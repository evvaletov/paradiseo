/*
<MPI_IslandModel.cpp>
Copyright (C) DOLPHIN Project-Team, INRIA Lille - Nord Europe, 2006-2012

Alexandre Quemy, Thibault Lasnier - INSA Rouen
Eremey Valetov

This software is governed by the CeCILL license under French law and
abiding by the rules of distribution of free software.  You can  use,
modify and/ or redistribute the software under the terms of the CeCILL
license as circulated by CEA, CNRS and INRIA at the following URL
"http://www.cecill.info".

ParadisEO WebSite : http://paradiseo.gforge.inria.fr
Contact: paradiseo-help@lists.gforge.inria.fr
*/

#include <functional>
#include <algorithm>
#include <mpi.h>
#include <SerializableBase.h>
#include <serial/Parser.h>
#include <utils/eoLogger.h>

template<class EOT>
MPI_IslandModel<EOT>::MPI_IslandModel(AbstractTopology& _topo, int _pollIntervalMs)
    : topo(&_topo), pollIntervalMs(_pollIntervalMs), running(false)
{
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_mpi_ranks);
}

template<class EOT>
void paradiseo::smp::MPI_IslandModel<EOT>::add(AIsland<EOT>& _island)
{
    islands.push_back(std::pair<AIsland<EOT>*, bool>(&_island, false));
    islands.back().first->setModel(this);
}

template<class EOT>
void MPI_IslandModel<EOT>::operator()() {
    running = true;
    std::future<void> islandTask;
    std::exception_ptr failure;
    try {
        initModel();
        if (mpi_rank < static_cast<int>(islands.size())) {
            auto* island = islands[mpi_rank].first;
            island->setRunning();
            islandTask = std::async(std::launch::async, &AIsland<EOT>::operator(), island);
        }
    } catch (...) {
        failure = std::current_exception();
    }

    int globalState[3] = {0, 0, 0};
    do {
        if (islandTask.valid() &&
            islandTask.wait_for(std::chrono::nanoseconds(0)) == std::future_status::ready) {
            try {
                islandTask.get();
            } catch (...) {
                if (!failure) failure = std::current_exception();
            }
        }
        if (!failure) {
            try {
                send();
            } catch (...) {
                failure = std::current_exception();
            }
        }
        int queued = 0;
        {
            std::lock_guard<std::mutex> lock(m);
            queued = listEmigrants.empty() ? 0 : 1;
        }
        int localState[3] = {islandTask.valid() ? 1 : 0, failure ? 1 : 0, queued};
        MPI_Allreduce(localState, globalState, 3, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        if (globalState[1] != 0) break;
        if (globalState[0] > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
    } while (globalState[0] > 0 || globalState[2] > 0);

    // Join the worker even when polling or the algorithm raised an exception.
    if (islandTask.valid()) {
        try {
            islandTask.get();
        } catch (...) {
            if (!failure) failure = std::current_exception();
        }
    }
    for (auto& message : sentMessages) {
        if (!message.valid()) continue;
        try {
            message.get();
        } catch (...) {
            if (!failure) failure = std::current_exception();
        }
    }
    sentMessages.clear();
    {
        std::lock_guard<std::mutex> lock(m);
        while (!listEmigrants.empty()) listEmigrants.pop();
    }
    // Cancellation of a send is not guaranteed to succeed. Keep receiving on
    // every rank until all posted messages have arrived, including eager sends
    // whose requests have already completed. No new sends are posted here.
    unsigned long long totals[2] = {0, 0};
    do {
        try {
            receiveMessages(!failure && globalState[1] == 0);
        } catch (...) {
            if (!failure) failure = std::current_exception();
        }
        completePendingSends();
        unsigned long long local[2] = {messagesSent, messagesReceived};
        MPI_Allreduce(local, totals, 2, MPI_UNSIGNED_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    } while (totals[1] < totals[0]);

    for (auto& ps : pendingSends)
        MPI_Wait(&ps.request, MPI_STATUS_IGNORE);
    pendingSends.clear();
    messagesSent = messagesReceived = 0;
    for (auto& message : sentMessages) {
        if (!message.valid()) continue;
        try {
            message.get();
        } catch (...) {
            if (!failure) failure = std::current_exception();
        }
    }
    sentMessages.clear();
    if (!failure && globalState[1] == 0 && mpi_rank < static_cast<int>(islands.size())) {
        try {
            islands[mpi_rank].first->receive();
        } catch (...) {
            failure = std::current_exception();
        }
    }
    running = false;

    int localFailure = failure ? 1 : 0;
    int globalFailure = 0;
    MPI_Allreduce(&localFailure, &globalFailure, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (failure) std::rethrow_exception(failure);
    if (globalFailure) throw std::runtime_error("An MPI island failed on another rank");
}

template<class EOT>
bool paradiseo::smp::MPI_IslandModel<EOT>::update(eoPop<EOT> _data, AIsland<EOT>* _island)
{
    std::lock_guard<std::mutex> lock(m);
    listEmigrants.push(std::pair<eoPop<EOT>,AIsland<EOT>*>(_data, _island));
    return true;
}

template<class EOT>
void paradiseo::smp::MPI_IslandModel<EOT>::setTopology(AbstractTopology& _topo)
{
    std::lock_guard<std::mutex> lock(m);
    topo = &_topo;
    if (running) {
        topo->construct(islands.size());
        for (auto it : islands)
            if (!it.second)
                topo->isolateNode(table.getLeft()[it.first]);
    }
}

template<class EOT>
void paradiseo::smp::MPI_IslandModel<EOT>::send(void) {
    receiveMessages(true);

    // Clean up completed async tasks
    for (auto it = sentMessages.begin(); it != sentMessages.end();) {
        if (it->wait_for(std::chrono::nanoseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        auto completed = *it;
        it = sentMessages.erase(it);
        completed.get();
    }

    // Then send outgoing emigrants
    eoPop<EOT> migPop;
    unsigned idFrom = 0;
    bool hasMigrant = false;
    {
        std::lock_guard<std::mutex> lock(m);
        if (!listEmigrants.empty()) {
            idFrom = table.getLeft()[listEmigrants.front().second];
            migPop = std::move(listEmigrants.front().first);
            listEmigrants.pop();
            hasMigrant = true;
        }
    }
    if (hasMigrant) {
        std::vector<unsigned> neighbors;
        {
            std::lock_guard<std::mutex> lock(m);
            neighbors = topo->getIdNeighbors(idFrom);
        }

        // Serialize once for all neighbors (same protocol as comm.send for Persistent)
        SerializableBase<eoPop<EOT>> serializablePop(migPop);
        std::unique_ptr<eoserial::Object> obj(serializablePop.pack());
        std::stringstream ss;
        obj->print(ss);
        std::string serialized = ss.str();
        int size = static_cast<int>(serialized.size()) + 1;

        for (unsigned idTo : neighbors) {
            const int tag = migrationTag;
            eo::log << eo::debug << "MPI_IslandModel: rank " << mpi_rank
                    << " sending " << migPop.size() << " migrant(s) from island "
                    << idFrom << " to island " << idTo << " (tag=" << tag << ")" << std::endl;

            // Single non-blocking send. The receiver uses MPI_Get_count
            // from MPI_Iprobe to determine the size, eliminating the
            // separate blocking size message that could deadlock.
            pendingSends.emplace_back();
            PendingSend& ps = pendingSends.back();
            ps.buffer = serialized;
            MPI_Isend(ps.buffer.data(), size, MPI_CHAR, idTo, tag,
                      MPI_COMM_WORLD, &ps.request);
            ++messagesSent;
        }
    }

    completePendingSends();
}

template<class EOT>
void paradiseo::smp::MPI_IslandModel<EOT>::receiveMessages(bool deliver) {
    if (!islands.empty() && mpi_rank < static_cast<int>(islands.size())) {
        unsigned myId = mpi_rank;
        // Incoming edges need not match this island's outgoing neighbors.
        // Also accept messages sent before a topology change.
        for (int idFromNeighbor = 0; idFromNeighbor < num_mpi_ranks; ++idFromNeighbor) {
            const int tag = migrationTag;
            int flag = 0;
            MPI_Status mpiStatus;
            MPI_Iprobe(idFromNeighbor, tag, MPI_COMM_WORLD, &flag, &mpiStatus);

            if (flag) {
                // Single-message receive: MPI_Iprobe confirmed the complete
                // message is available, so MPI_Recv returns immediately.
                // This avoids the two-message protocol (size + data) in
                // comm.recv(), which can deadlock when the size message
                // arrives before the data message is progressed by MPI.
                int count = 0;
                MPI_Get_count(&mpiStatus, MPI_CHAR, &count);
                std::string serialized(count, '\0');
                MPI_Recv(&serialized[0], count, MPI_CHAR, idFromNeighbor, tag,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                ++messagesReceived;
                if (!deliver) continue;

                std::unique_ptr<eoserial::Object> obj(eoserial::Parser::parse(serialized));
                SerializableBase<eoPop<EOT>> receivedSerializablePop;
                receivedSerializablePop.unpack(obj.get());

                eoPop<EOT> receivedPop = receivedSerializablePop;
                eo::log << eo::debug << "MPI_IslandModel: rank " << mpi_rank
                        << " received " << receivedPop.size() << " migrant(s) from island "
                        << idFromNeighbor << " (tag=" << tag << ")" << std::endl;

                sentMessages.push_back(std::async(std::launch::async,
                    &AIsland<EOT>::update,
                    table.getRight()[myId],
                    std::move(receivedPop)));
            }
        }
    }

}

template<class EOT>
void paradiseo::smp::MPI_IslandModel<EOT>::completePendingSends() {
    auto it = pendingSends.begin();
    while (it != pendingSends.end()) {
        int completed = 0;
        MPI_Test(&it->request, &completed, MPI_STATUS_IGNORE);
        if (completed)
            it = pendingSends.erase(it);
        else
            ++it;
    }
}

template<class EOT>
bool paradiseo::smp::MPI_IslandModel<EOT>::isRunning() const
{
    return (bool)running;
}

template<class EOT>
void paradiseo::smp::MPI_IslandModel<EOT>::initModel(void)
{
    if (num_mpi_ranks > static_cast<int>(islands.size()) + 1) {
        eo::log << eo::errors << "MPI_IslandModel: number of MPI ranks ("
                << num_mpi_ranks << ") exceeds number of islands + 1 ("
                << islands.size() + 1 << ")" << std::endl;
        throw std::invalid_argument("MPI_IslandModel: number of MPI ranks exceeds number of islands + 1");
    }

    std::lock_guard<std::mutex> lock(m);
    for (auto& it : islands)
        it.second = true;

    topo->construct(islands.size());
    table = createTable();
}

template<class EOT>
Bimap<unsigned, AIsland<EOT>*> paradiseo::smp::MPI_IslandModel<EOT>::createTable()
{
    Bimap<unsigned, AIsland<EOT>*> table;
    unsigned islandId = 0;
    for (auto it : islands) {
        table.add(islandId, it.first);
        islandId++;
    }
    return table;
}
