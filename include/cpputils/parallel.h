
#ifndef INDEX_MAINTAIN_PARALLEL_H
#define INDEX_MAINTAIN_PARALLEL_H

#include <queue>
#include <thread>
#include <condition_variable>
#include <functional>

#include "cpputils/sync_ostream.h"

#include <iostream>

namespace cpputils {

    template<class MsgType, class QueType = std::queue<MsgType>>
    class ProCon {
    protected:

        std::condition_variable consume_cond, produce_cond;
        std::mutex mtx_msg;
        std::mutex mtx_producer, mtx_consumer;
        int produce_msgs = 0, consume_msgs = 0;
    public:
        virtual ~ProCon() = default;

        QueType q;
        int msgs_buffer_size = 500000;

        virtual void produce(MsgType msg) = 0;
        virtual bool need_produce() = 0;

        virtual MsgType consume() = 0;
        virtual bool need_consume() = 0;
        size_t size() {
            return q.size();
        }
    };

    template<class MsgType>
    class Generator: public ProCon<MsgType> {
    private:
        int producer_counter = 1;
        bool produce_finished = false;
    public:
        int max_ops = 100000;

        void produce(MsgType msg) override;
        bool need_produce() override;
        void finish_produce();

        MsgType consume() override;
        bool need_consume() override;
        std::pair<bool, MsgType> try_consume();
    };

    template<class MsgType>
    class Processor: public ProCon<MsgType> {
    private:
        bool produce_finished = false, consume_stop_msg = false;
    public:
        std::string name;

        void produce(MsgType msg) override;
        bool need_produce() override;
        void finish_produce();

        MsgType consume() override;
        bool need_consume() override;
        bool consumed_stop_msg() {
            return consume_stop_msg;
        }
        void stop() {
            consume_stop_msg = true;
        }
    };

    template<class MsgType>
    inline void Generator<MsgType>::produce(MsgType msg) {
        std::unique_lock lck{this->mtx_msg};
        this->produce_cond.wait(lck, [&]{return this->msgs_buffer_size < 0 || this->q.size() < this->msgs_buffer_size;});
        this->q.push(std::move(msg));
        this->produce_msgs++;
        this->consume_cond.notify_one();
        lck.unlock();
    }

    template<class MsgType>
    inline bool Generator<MsgType>::need_produce() {
        std::unique_lock lop{this->mtx_producer};
        if (this->produce_msgs >= max_ops) {

            cpputils::sos.lock();

            std::cout << "Producer Thread-" << std::this_thread::get_id() << " finished." << std::endl;
            std::cout << "current_ops: " << this->produce_msgs << std::endl;
            cpputils::sos.unlock();
            lop.unlock();
            return false;
        }
        lop.unlock();
        return true;
    }

    template<class MsgType>
    inline MsgType Generator<MsgType>::consume() {
        std::unique_lock lck{this->mtx_msg};

        this->consume_cond.wait(lck, [this]{return !this->q.empty() || (this->q.empty() && produce_finished);});

        auto m = this->q.front();
        this->q.pop();
        this->consume_msgs++;
        this->produce_cond.notify_one();
        lck.unlock();
        return m;
    }

    template<class MsgType>
    std::pair<bool, MsgType> Generator<MsgType>::try_consume() {
        std::unique_lock lck{this->mtx_msg};
        this->consume_cond.wait(lck, [this]{return !this->q.empty() || (produce_finished);});
        if (produce_finished && this->q.empty()) {
            cpputils::sos.lock();
            std::cout << "Consumer Thread-" << std::this_thread::get_id() << " finished." << std::endl;
            cpputils::sos.unlock();
            return std::make_pair(false, MsgType());
        }

        auto m = this->q.front();
        this->q.pop();
        this->consume_msgs++;
        this->produce_cond.notify_one();
        lck.unlock();
        return make_pair(true, m);
    }

    template<class MsgType>
    inline bool Generator<MsgType>::need_consume() {
        std::unique_lock lop{this->mtx_consumer};

        if (produce_finished && this->q.empty()) {
            cpputils::sos.lock();
            std::cout << "Consumer Thread-" << std::this_thread::get_id() << " finished." << std::endl;
            cpputils::sos.unlock();
            lop.unlock();
            return false;
        }
        lop.unlock();
        return true;
    }

    template<class MsgType>
    inline void Generator<MsgType>::finish_produce() {
        produce_finished = true;
        this->consume_cond.notify_all();
    }

    template<class MsgType>
    inline void Processor<MsgType>::produce(MsgType msg) {
        std::unique_lock lck{this->mtx_msg};
#ifdef DEBUG_QUEUE
        sos.lock();
        std::cout << "[queue producer wait], thread:" << name
             << ", produce_msgs: " << this->produce_msgs
             << ", consume_msgs: " << this->consume_msgs
             << ", queue size: " << this->q.size()  << std::endl;
        sos.unlock();
#endif
        this->produce_cond.wait(lck, [&]{return this->msgs_buffer_size < 0 || this->q.size() < this->msgs_buffer_size;});
        this->q.push(std::move(msg));
        this->produce_msgs++;
        lck.unlock();
        this->consume_cond.notify_one();
    }

    template<class MsgType>
    inline bool Processor<MsgType>::need_produce() {
        std::unique_lock lop{this->mtx_producer};
        lop.unlock();
        return true;
    }

    template<class MsgType>
    inline void Processor<MsgType>::finish_produce() {
        produce_finished = true;
        this->consume_cond.notify_all();
    }

    template<class MsgType>
    inline MsgType Processor<MsgType>::consume() {
        std::unique_lock lck{this->mtx_msg};
#ifdef DEBUG_QUEUE
        sos.lock();
        std::cout << "[queue consumer wait], thread:" << name
             << ", produce_msgs: " << this->produce_msgs
             << ", consume_msgs: " << this->consume_msgs
             << ", queue size: " << this->q.size() << ", produce_finished:" << produce_finished << std::endl;
        sos.unlock();
#endif
        this->consume_cond.wait(lck, [this]{return !this->q.empty() || (this->q.empty() && produce_finished);});

        if (produce_finished && this->q.empty()) {
            std::cout << "should not reach here" << std::endl;
            consume_stop_msg = true;
            return MsgType();
        }
        auto m = this->q.front();
        this->q.pop();
        this->consume_msgs++;
        lck.unlock();
        this->produce_cond.notify_one();
        return m;
    }

    template<class MsgType>
    inline bool Processor<MsgType>::need_consume() {
        std::unique_lock lop{this->mtx_consumer};
#ifdef PROCON_VERBOSE
        std::cout << "produce_finished: " << produce_finished << ", consume_stop_msg:" << consume_stop_msg << ", produce_msgs: " << this->produce_msgs << ", consume_msgs: " << this->consume_msgs << std::endl;
#endif
        if (produce_finished && this->consume_msgs >= this->produce_msgs) {
            cpputils::sos.lock();
            std::cout << "Consumer Thread-" << this->name << " " << std::this_thread::get_id() << " finished." << std::endl;
            cpputils::sos.unlock();
            consume_stop_msg = true;
            lop.unlock();
            return false;
        }
        lop.unlock();
        return true;
    }
}

#endif
