#include <eo>
#include <smp>
#include <algo/moeoAlgoFinalized.h>
#include <iostream>
#include <stdexcept>

using namespace paradiseo::smp;
using Individual = eoReal<double>;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct TestTopology : AbstractTopology {
    explicit TestTopology(std::vector<unsigned> targets) : targets(std::move(targets)) {}
    std::vector<unsigned> targets;
    unsigned constructions = 0;
    std::vector<unsigned> getIdNeighbors(unsigned source) const override {
        return source == 0 ? targets : std::vector<unsigned>{};
    }
    void construct(unsigned) override { ++constructions; }
    void isolateNode(unsigned) override {}
};

struct TestIsland : AIsland<Individual> {
    std::atomic<bool> stopped{false};
    eoPop<Individual> received;
    bool failRun = false;
    bool failReceive = false;
    void operator()() override {
        if (failRun) throw std::runtime_error("algorithm failure");
        stopped = true;
    }
    void setModel(IslandModel<Individual>*) override {}
    void check() override {}
    bool update(eoPop<Individual> data) override {
        std::lock_guard<std::mutex> lock(m);
        received.insert(received.end(), data.begin(), data.end());
        return true;
    }
    bool isStopped() const override { return stopped; }
    void setRunning() override { stopped = false; }
    void receive() override {
        if (failReceive) throw std::runtime_error("integration failure");
    }
};

struct CompletedIsland : TestIsland {
    bool isStopped() const override {
        // Force the coordinator to observe completion before testing its
        // treatment of batches that were queued before the workers ran.
        while (!stopped) std::this_thread::yield();
        return true;
    }
};

struct ModelProbe : IslandModel<Individual> {
    using IslandModel::IslandModel;
    void initialize() { initModel(); running = true; }
    void deliver() {
        send();
        for (auto& message : sentMessages) message.get();
        sentMessages.clear();
    }
    void setDeliveries(std::vector<std::shared_future<bool>> messages) {
        sentMessages = std::move(messages);
    }
    bool validDeliveries() const {
        for (const auto& message : sentMessages)
            if (!message.valid()) return false;
        return true;
    }
    void finishDeliveries() {
        for (auto& message : sentMessages) message.get();
        sentMessages.clear();
    }
};

struct Evaluate : eoEvalFunc<Individual> {
    void operator()(Individual& x) override { x.fitness(0.); }
};

template<class EOT>
struct ThrowingAlgorithm {
    ThrowingAlgorithm(eoContinue<EOT>&, eoEvalFunc<EOT>&) {}
    void operator()(eoPop<EOT>&) { throw std::runtime_error("wrapped algorithm failure"); }
};

template<class EOT>
struct FinalizingAlgorithm : moeoAlgoFinalized<EOT> {
    int& calls;
    FinalizingAlgorithm(eoContinue<EOT>&, eoEvalFunc<EOT>&, int& calls) : calls(calls) {}
    void operator()(eoPop<EOT>&) {}
    void finalize(eoPop<EOT>&) override { ++calls; }
};
}

int main()
{
    try {
        TestTopology oldTopology({1}), replacement({2}), broadcast({1, 2});
        TestIsland a, b, c;
        ModelProbe model(oldTopology);
        model.add(a); model.add(b); model.add(c);
        model.setTopology(replacement);
        model.initialize();
        require(oldTopology.constructions == 0 && replacement.constructions == 1,
                "setTopology did not replace the topology before initialization");
        eoPop<Individual> migrants;
        migrants.resize(1);
        migrants[0].resize(1); migrants[0][0] = 7.; migrants[0].fitness(3.);
        model.update(migrants, &a);
        model.deliver();
        require(b.received.empty() && c.received.size() == 1,
                "migration used the original topology");

        model.setTopology(broadcast);
        require(broadcast.constructions == 1, "running topology was not constructed");
        model.update(migrants, &a);
        model.deliver();
        require(b.received.size() == 1 && c.received.size() == 2,
                "broadcast did not send the population to every neighbor");
        require(b.received[0][0] == 7. && c.received[1][0] == 7., "migrant payload changed");

        Topology<Complete> complete;
        CompletedIsland source, first, second;
        IslandModel<Individual> queuedModel(complete);
        queuedModel.add(source); queuedModel.add(first); queuedModel.add(second);
        for (unsigned batch = 0; batch < 11; ++batch)
            queuedModel.update(migrants, &source);
        queuedModel();
        require(first.received.size() == 11 && second.received.size() == 11,
                "shared shutdown discarded queued migrants");
        require(first.received.back()[0] == 7. && second.received.back()[0] == 7.,
                "shared shutdown corrupted a queued migrant");

        std::promise<bool> completed, pending, failed;
        model.setDeliveries({completed.get_future().share(), pending.get_future().share(),
                             failed.get_future().share()});
        completed.set_value(true);
        failed.set_exception(std::make_exception_ptr(std::runtime_error("delivery failure")));
        model.update(migrants, &a);
        bool deliveryFailed = false;
        try { model.deliver(); } catch (const std::runtime_error&) { deliveryFailed = true; }
        pending.set_value(true);
        require(deliveryFailed && model.validDeliveries(),
                "reaping a failed delivery left invalid futures in the queue");
        model.finishDeliveries();

        for (bool failRun : {true, false}) {
            TestTopology topology({});
            TestIsland island;
            island.failRun = failRun;
            island.failReceive = !failRun;
            IslandModel<Individual> failingModel(topology);
            failingModel.add(island);
            bool caught = false;
            try { failingModel(); } catch (const std::runtime_error&) { caught = true; }
            require(caught && !failingModel.isRunning(), "worker failure was not propagated/cleaned up");
            island.failRun = island.failReceive = false;
            failingModel();
            require(!failingModel.isRunning(), "model could not run again after failure");
        }

        eoPop<Individual> population;
        eoGenContinue<Individual> continuation(1);
        Evaluate eval;
        eoPlusReplacement<Individual> integration;
        MigPolicy<Individual> migration;
        Island<ThrowingAlgorithm, Individual> wrapped(
            population, integration, migration, continuation, eval);
        TestTopology topology({});
        IslandModel<Individual> failingModel(topology);
        failingModel.add(wrapped);
        bool caught = false;
        try { failingModel(); } catch (const std::runtime_error&) { caught = true; }
        require(caught && wrapped.isStopped() && !failingModel.isRunning(),
                "wrapped algorithm failure did not stop the island");

        int finalizations = 0;
        population = migrants;
        Island<FinalizingAlgorithm, Individual> finalized(
            population, integration, migration, continuation, eval, finalizations);
        finalized.update(migrants);
        finalized.receive();
        require(finalizations == 1, "integration did not invoke the optional finalize hook");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
