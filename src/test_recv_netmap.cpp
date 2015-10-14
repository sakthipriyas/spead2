/* Copyright 2015 SKA South Africa
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <utility>
#include <chrono>
#include <cstdint>
#include <boost/asio.hpp>
#include "common_thread_pool.h"
#include "recv_netmap.h"
#include "recv_heap.h"
#include "recv_live_heap.h"
#include "recv_ring_stream.h"

typedef std::chrono::time_point<std::chrono::high_resolution_clock> time_point;

static time_point start = std::chrono::high_resolution_clock::now();
static std::uint64_t n_complete = 0;

class trivial_stream : public spead2::recv::stream
{
private:
    virtual void heap_ready(spead2::recv::live_heap &&heap) override
    {
        std::cout << "Got heap " << heap.get_cnt();
        if (heap.is_complete())
        {
            std::cout << " [complete]\n";
            n_complete++;
        }
        else if (heap.is_contiguous())
            std::cout << " [contiguous]\n";
        else
            std::cout << " [incomplete]\n";
    }

    std::promise<void> stop_promise;

public:
    using spead2::recv::stream::stream;

    virtual void stop_received() override
    {
        spead2::recv::stream::stop_received();
        stop_promise.set_value();
    }

    void join()
    {
        std::future<void> future = stop_promise.get_future();
        future.get();
    }
};

void show_heap(const spead2::recv::heap &fheap)
{
    std::cout << "Received heap with CNT " << fheap.get_cnt() << '\n';
    const auto &items = fheap.get_items();
    std::cout << items.size() << " item(s)\n";
    for (const auto &item : items)
    {
        std::cout << "    ID: 0x" << std::hex << item.id << std::dec << ' ';
        std::cout << "[" << item.length << " bytes]";
        std::cout << '\n';
    }
    std::vector<spead2::descriptor> descriptors = fheap.get_descriptors();
    for (const auto &descriptor : descriptors)
    {
        std::cout
            << "    0x" << std::hex << descriptor.id << std::dec << ":\n"
            << "        NAME:  " << descriptor.name << "\n"
            << "        DESC:  " << descriptor.description << "\n";
        if (descriptor.numpy_header.empty())
        {
            std::cout << "        TYPE:  ";
            for (const auto &field : descriptor.format)
                std::cout << field.first << field.second << ",";
            std::cout << "\n";
            std::cout << "        SHAPE: ";
            for (const auto &size : descriptor.shape)
                if (size == -1)
                    std::cout << "?,";
                else
                    std::cout << size << ",";
            std::cout << "\n";
        }
        else
            std::cout << "        DTYPE: " << descriptor.numpy_header << "\n";
    }
    time_point now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = now - start;
    std::cout << elapsed.count() << "\n";
    std::cout << std::flush;
}

static void run_trivial()
{
    spead2::thread_pool worker;
    trivial_stream stream(worker);
    stream.emplace_reader<spead2::recv::netmap_udp_reader>(std::string("lo"), 8888);
    stream.join();
}

static void run_ringbuffered()
{
    spead2::thread_pool worker;
    std::shared_ptr<spead2::memory_pool> pool = std::make_shared<spead2::memory_pool>(16384, 26214400, 12, 8);
    spead2::recv::ring_stream<> stream(worker, 7);
    stream.set_memory_pool(pool);
    stream.emplace_reader<spead2::recv::netmap_udp_reader>(std::string("lo"), 8888);
    while (true)
    {
        try
        {
            spead2::recv::heap fh = stream.pop();
            n_complete++;
            show_heap(fh);
        }
        catch (spead2::ringbuffer_stopped &e)
        {
            break;
        }
    }
}

int main()
{
    // run_trivial();
    run_ringbuffered();
    std::cout << "Received " << n_complete << " complete heaps\n";
    return 0;
}
