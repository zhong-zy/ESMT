
#ifndef INDEX_MAINTAIN_PARALLEL_ORDER_H
#define INDEX_MAINTAIN_PARALLEL_ORDER_H
#include "parallel.h"

namespace cpputils {
    template<class MsgType, class QueType>
    class OrderProcessor: public ProCon<MsgType, QueType> {
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

    template<class MsgType, class QueType>
    inline void OrderProcessor<MsgType, QueType>::produce(MsgType msg) {
        std::unique_lock lck{this->mtx_msg};
#ifdef DEBUG_QUEUE
        sos.lock();
        std::cout << "[priority_queue producer wait], thread:" << name
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

    template<class MsgType, class QueType>
    inline bool OrderProcessor<MsgType, QueType>::need_produce() {
        std::unique_lock lop{this->mtx_producer};
        lop.unlock();
        return true;
    }

    template<class MsgType, class QueType>
    inline void OrderProcessor<MsgType, QueType>::finish_produce() {
        produce_finished = true;
        this->consume_cond.notify_all();
    }

    template<class MsgType, class QueType>
    inline MsgType OrderProcessor<MsgType, QueType>::consume() {
        std::unique_lock lck{this->mtx_msg};
#ifdef DEBUG_QUEUE
        sos.lock();
        std::cout << "[priority_queue consumer wait], thread:" << name
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
        auto m = this->q.top();
        this->q.pop();
        this->consume_msgs++;
        lck.unlock();
        this->produce_cond.notify_one();
        return m;
    }

    template<class MsgType, class QueType>
    inline bool OrderProcessor<MsgType, QueType>::need_consume() {
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
