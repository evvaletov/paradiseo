#include <eo>
#include <smp>
#include <iostream>
#include <stdexcept>

using namespace paradiseo::smp;
using Individual = eoReal<double>;

namespace {
void require(bool condition, const char* message)
{
    int local = condition ? 1 : 0, all = 0;
    MPI_Allreduce(&local, &all, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!all) throw std::runtime_error(message);
}

struct TestTopology : AbstractTopology {
    explicit TestTopology(bool connected) : connected(connected) {}
    bool connected;
    bool directed = false;
    bool fail = false;
    unsigned size = 0;
    unsigned constructions = 0;
    std::vector<unsigned> getIdNeighbors(unsigned source) const override {
        if (fail) throw std::runtime_error("polling failure");
        std::vector<unsigned> result;
        if (directed) return {(source + 1) % size};
        if (connected)
            for (unsigned i = 0; i < size; ++i)
                if (i != source) result.push_back(i);
        return result;
    }
    void construct(unsigned n) override { size = n; ++constructions; }
    void isolateNode(unsigned) override {}
};

struct TestIsland : AIsland<Individual> {
    std::atomic<bool> stopped{false};
    std::atomic<unsigned> received{0};
    std::atomic<bool> badPayload{false};
    unsigned delayMs = 5;
    bool failRun = false;
    bool failReceive = false;
    bool failDelivery = false;
    void operator()() override {
        if (failRun) throw std::runtime_error("algorithm failure");
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        stopped = true;
    }
    void setModel(IslandModel<Individual>*) override {}
    void check() override {}
    bool update(eoPop<Individual> data) override {
        if (failDelivery) throw std::runtime_error("delivery failure");
        if (data.size() != 1 || data[0].size() != 8192 ||
            data[0][0] != 7. || data[0].back() != 7. || data[0].fitness() != 3.)
            badPayload = true;
        ++received;
        return true;
    }
    bool isStopped() const override { return stopped; }
    void setRunning() override { stopped = false; }
    void receive() override {
        if (failReceive) throw std::runtime_error("integration failure");
    }
};

struct ModelProbe : MPI_IslandModel<Individual> {
    using MPI_IslandModel::MPI_IslandModel;
    static_assert(migrationTag >= 0 && migrationTag <= 32767, "tag exceeds MPI's guaranteed range");
    void initialize() { initModel(); }
    void poll() { send(); }
};
}

int main(int argc, char** argv)
{
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    int rank = 0, ranks = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
    int failed = 0;
    try {
        require(provided >= MPI_THREAD_FUNNELED && ranks >= 2, "test needs MPI threads and at least two ranks");
        TestTopology empty(false), complete(true);
        MPI_IslandModel<Individual> invalidModel(empty, 0);
        bool invalidConfiguration = false;
        try { invalidModel(); }
        catch (const std::invalid_argument&) { invalidConfiguration = true; }
        require(invalidConfiguration && !invalidModel.isRunning(),
                "invalid MPI configuration did not propagate an exception");

        std::vector<std::unique_ptr<TestIsland>> islands;
        ModelProbe model(empty, 0);
        for (int i = 0; i < ranks; ++i) {
            islands.emplace_back(new TestIsland);
            model.add(*islands.back());
        }
        model.setTopology(complete);
        model.initialize();
        require(empty.constructions == 0 && complete.constructions == 1,
                "MPI topology was not replaced");

        eoPop<Individual> migrants;
        migrants.resize(1);
        migrants[0].resize(8192, 7.); migrants[0].fitness(3.);
        for (bool directed : {false, true}) {
            complete.directed = directed;
            islands[rank]->received = 0;
            model.update(migrants, islands[rank].get());
            int allReceived = 0;
            for (unsigned attempt = 0; attempt < 10000 && !allReceived; ++attempt) {
                model.poll();
                unsigned expected = directed ? 1u : static_cast<unsigned>(ranks - 1);
                int localReceived = islands[rank]->received == expected;
                MPI_Allreduce(&localReceived, &allReceived, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
                if (!allReceived) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            require(allReceived && !islands[rank]->badPayload,
                    "MPI migration lost or corrupted a population");
            model(); // Join delivery tasks and release pending MPI requests.
            require(!model.isRunning(), "MPI model remained running");
        }

        complete.directed = true;
        islands[rank]->received = 0;
        islands[rank]->delayMs = 0;
        for (unsigned batch = 0; batch < 11; ++batch)
            model.update(migrants, islands[rank].get());
        model();
        require(islands[rank]->received == 11, "normal shutdown discarded queued migrants");

        for (int mode = 0; mode < 4; ++mode) {
            islands[rank]->failRun = rank == 0 && mode == 0;
            complete.fail = rank == 0 && mode == 1;
            islands[rank]->failReceive = rank == 0 && mode == 2;
            islands[rank]->failDelivery = rank == 0 && mode == 3;
            if (mode == 1 || mode == 3) model.update(migrants, islands[rank].get());
            bool caught = false;
            try { model(); } catch (const std::runtime_error&) { caught = true; }
            require(caught && !model.isRunning(), "MPI failure was not propagated on every rank");
            islands[rank]->failRun = islands[rank]->failReceive =
                islands[rank]->failDelivery = complete.fail = false;
            model();
            require(!model.isRunning(), "MPI model could not run again after failure");
        }
    } catch (const std::exception& error) {
        std::cerr << "rank " << rank << ": " << error.what() << '\n';
        failed = 1;
    }
    int anyFailed = 0;
    MPI_Allreduce(&failed, &anyFailed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    MPI_Finalize();
    return anyFailed;
}
