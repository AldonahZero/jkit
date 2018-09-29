#ifndef FIBER_UNBOUNDED_BUFFER_H
#define FIBER_UNBOUNDED_BUFFER_H

#include <boost/utility.hpp>
#include <queue>
#include <mutex>
#include <boost/fiber/mutex.hpp>
#include <boost/fiber/condition_variable.hpp>

template<typename T>
class fiber_unbounded_queue : private boost::noncopyable
{
public:
    fiber_unbounded_queue()
    {
    }

    void push(T&& t)
    {
        std::unique_lock<boost::fibers::mutex> lk(m_mutex);
        while (m_max_count == m_buffers.size())
            m_buf_not_full.wait(lk);

        m_buffers.push(t);
        m_buf_not_empty.notify_one();
    }

    void push(const T& t)
    {
        std::unique_lock<boost::fibers::mutex> lk(m_mutex);
        while (m_max_count == m_buffers.size())
            m_buf_not_full.wait(lk);

        m_buffers.push(t);
        m_buf_not_empty.notify_one();
    }

    T pop()
    {
        std::unique_lock<boost::fibers::mutex> lk(m_mutex);
        while (m_buffers.size() == 0)
            m_buf_not_empty.wait(lk);

        T t = std::move(m_buffers.front());
        m_buffers.pop();
        m_buf_not_full.notify_one();
        return std::move(t);
    }
private:
    size_t m_max_count = 1000000;
    std::queue<T> m_buffers;
    boost::fibers::condition_variable m_buf_not_full;
    boost::fibers::condition_variable m_buf_not_empty;
    boost::fibers::mutex m_mutex;
};

#endif // FIBER_UNBOUNDED_BUFFER_H
