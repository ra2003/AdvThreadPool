#include "advthread.h"
#include "advthreadpool.h"
//
#include <QTime>
#include <QCoreApplication>
#include <QMutexLocker>
//
#include <iostream>

AdvThreadPool::AdvThreadPool(QObject *parent) :
    QObject(parent)
{
    m_uiMaxThreadNumber = 0;
    m_uiUniquer         = 0;
    m_EnableWork        = false;
}

AdvThreadPool::~AdvThreadPool()
{
    ThreadMap::Iterator itr;
    for (itr = m_Pool.begin(); itr != m_Pool.end(); ++itr)
    {
        delete itr.value();
    };
    //
    m_Pool.clear();
}

AdvThreadPool& AdvThreadPool::getInstance()
{
    static AdvThreadPool instance;
    //
    return instance;
}

void AdvThreadPool::init(unsigned int ui_threads_amount)
{
    if (m_uiMaxThreadNumber > 0)
        return;
    //
    if (ui_threads_amount > 0)
        m_uiMaxThreadNumber = ui_threads_amount;
    else
        m_uiMaxThreadNumber = QThread::idealThreadCount();
    //
    m_EnableWork = true;
}

bool AdvThreadPool::execute(AdvThreadJob* ptr_job)
{
    if ( NULL == ptr_job )
        return false;
    //
    if (false == m_EnableWork)
        return false;
    //
    if (getTaskQueueSize() > 0)
    {
        addJobToQueue(ptr_job);
        //std::cout<<"add job N 1, queuse size:"<<getTaskQueueSize()<<std::endl;
        return true;
    };
    //
    AdvThread* ptr_available_thread = reserveAvailableThread(); // scan threadpool, search available thread
    //
    if (NULL == ptr_available_thread)   // available thread was not found, may be all existing threads are busy
    {
        ptr_available_thread = createReservedThread();
    };
    //
    if (NULL != ptr_available_thread)
    {
        ptr_available_thread->setJob(ptr_job);
        ptr_available_thread->start(ptr_job->getPriority());
    }else
    {
        addJobToQueue(ptr_job);
        //std::cout<<"add job N 2, queuse size:"<<getTaskQueueSize()<<std::endl;
    };
    //
    return true;
}

void AdvThreadPool::stop (bool b_wait_until_idle)
{
    m_EnableWork = false;
    //
    if (b_wait_until_idle)
    {
        while (isIdle() == false)
        {
            QTime dieTime= QTime::currentTime().addSecs(1);
            //
            while( QTime::currentTime() < dieTime )
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            };
        };
    };
    //
    Q_ASSERT(getTaskQueueSize() == 0);
    return;
}

bool AdvThreadPool::isIdle ()
{
    ThreadMap::Iterator itr;
    {
        QMutexLocker thread_locker (&m_PoolLocker);
        //
        for (itr = m_Pool.begin(); itr != m_Pool.end(); ++itr)
        {
            if ( itr.value()->isActive() )
            {
                return false;
            };
        };
    }
    //
    return true;
}

void AdvThreadPool::onFinishThread(unsigned int ui_thread_id)
{
    //std::cout<<"finish triggered, queuse size:"<<getTaskQueueSize()<<std::endl;
    AdvThreadJob* ptr_job = NULL;
    //
    if ( false == m_Pool.contains(ui_thread_id) )
    {
        Q_ASSERT(FALSE);
        return;
    }
    //
    if (getTaskQueueSize() > 0)
    {
        QMutexLocker locker (&m_PoolLocker);
        //
        if ( m_Pool[ui_thread_id]->isIdle() == false )
        {
            //this thread is already in use. leave this function, another thread will catch the next job
            return;
        };
        //
        ptr_job = extractNextJob();
        //
        if ( ptr_job )
        {
            m_Pool[ui_thread_id]->setJob(ptr_job);
            m_Pool[ui_thread_id]->start( ptr_job->getPriority() );
        };
    };
}

int AdvThreadPool::getTaskQueueSize() const
{
    int i_queue_size = 0;
    {
        QMutexLocker locker (&m_TaskLocker);
        i_queue_size = m_TaskQueue.size();
    }
    //
    return i_queue_size;
}

void AdvThreadPool::addJobToQueue   (AdvThreadJob* ptr_job)
{
        QMutexLocker locker (&m_TaskLocker);
        m_TaskQueue.enqueue(ptr_job);
}

AdvThreadJob*   AdvThreadPool::extractNextJob ()
{
    QMutexLocker locker (&m_TaskLocker);
    //
    AdvThreadJob* ptr_job = NULL;
    //
    if (m_TaskQueue.size() > 0)
        ptr_job = m_TaskQueue.dequeue();
    //
    return ptr_job;
}

AdvThread*  AdvThreadPool::reserveAvailableThread ()
{
    AdvThread* ptr_available_thread = NULL;
    QMutexLocker thread_locker (&m_PoolLocker);
    //
    ThreadMap::Iterator itr;
    for (itr = m_Pool.begin(); itr != m_Pool.end(); ++itr)
    {
        if ( itr.value()->isIdle() )
        {
            ptr_available_thread = itr.value();
            ptr_available_thread->reserve();
            break;
        };
    };
    //
    return ptr_available_thread;
}

AdvThread* AdvThreadPool::createReservedThread ()
{
    QMutexLocker thread_locker (&m_PoolLocker);
    //
    AdvThread* ptr_thread = NULL;

    if ((unsigned int) m_Pool.size() < m_uiMaxThreadNumber)    // is it possible to create new one?
    {
        const unsigned int ui_id = ++m_uiUniquer;
        ptr_thread = new AdvThread(ui_id);
        ptr_thread->reserve();
        QObject::connect(ptr_thread, SIGNAL(ThreadFinished(unsigned int)), this, SLOT( onFinishThread(unsigned int) ));
        //
        m_Pool[ui_id] = ptr_thread;
    };
    //
    return ptr_thread;
}