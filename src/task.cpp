#include "task.h"
#include <iostream>
#include <string.h>
#include <string>
#include <algorithm>
#include "scheduler.h"

namespace co
{

uint64_t Task::s_id = 0;
std::atomic<uint64_t> Task::s_task_count{0};

Task::DeleteList Task::s_delete_list;
LFLock Task::s_delete_list_lock;

static void C_func(Task* self)
{
    if (g_Scheduler.GetOptions().exception_handle == eCoExHandle::immedaitely_throw) {
        (self->fn_)();
    } else {
        try {
            (self->fn_)();
        } catch (std::exception& e) {
            switch (g_Scheduler.GetOptions().exception_handle) {
                case eCoExHandle::immedaitely_throw:
                    throw ;
                    break;

                case eCoExHandle::delay_rethrow:
                    self->eptr_ = std::current_exception();
                    break;

                default:
                case eCoExHandle::debugger_only:
                    DebugPrint(dbg_exception, "task(%s) has uncaught exception:%s",
                            self->DebugInfo(), e.what());
                    break;
            }
        } catch (...) {
            switch (g_Scheduler.GetOptions().exception_handle) {
                case eCoExHandle::immedaitely_throw:
                    throw ;
                    break;

                case eCoExHandle::delay_rethrow:
                    self->eptr_ = std::current_exception();
                    break;

                default:
                case eCoExHandle::debugger_only:
                    DebugPrint(dbg_exception, "task(%s) has uncaught exception.", self->DebugInfo());
                    break;
            }
        }
    }

    self->state_ = TaskState::done;
    Scheduler::getInstance().CoYield();
}

Task::Task(TaskF const& fn)
    : id_(++s_id), fn_(fn)
{
    ++s_task_count;
}

Task::~Task()
{
    --s_task_count;
}

void Task::AddIntoProcesser(Processer *proc, char* shared_stack, uint32_t shared_stack_cap)
{
    assert(!proc_);
    proc_ = proc;
    if (!ctx_.Init([this]{C_func(this);}, shared_stack, shared_stack_cap)) {
        state_ = TaskState::fatal;
        fprintf(stderr, "task(%s) init, getcontext error:%s\n",
                DebugInfo(), strerror(errno));
        return ;
    }

    state_ = TaskState::runnable;
}

bool Task::SwapIn()
{
    return ctx_.SwapIn();
}
bool Task::SwapOut()
{
    return ctx_.SwapOut();
}

void Task::SetDebugInfo(std::string const& info)
{
    debug_info_ = info + "(" + std::to_string(id_) + ")";
}

const char* Task::DebugInfo()
{
    if (debug_info_.empty())
        debug_info_ = std::to_string(id_);

    return debug_info_.c_str();
}

uint64_t Task::GetTaskCount()
{
    return s_task_count;
}

void Task::SwapDeleteList(DeleteList &output)
{
    std::unique_lock<LFLock> lock(s_delete_list_lock);
    s_delete_list.swap(output);
}

std::size_t Task::GetDeletedTaskCount()
{
    std::unique_lock<LFLock> lock(s_delete_list_lock);
    return s_delete_list.size();
}

void Task::IncrementRef()
{
    DebugPrint(dbg_task, "task(%s) IncrementRef ref=%d",
            DebugInfo(), (int)ref_count_);
    ++ref_count_;
}

void Task::DecrementRef()
{
    DebugPrint(dbg_task, "task(%s) DecrementRef ref=%d",
            DebugInfo(), (int)ref_count_);
    if (--ref_count_ == 0) {
        std::unique_lock<LFLock> lock(s_delete_list_lock);
        assert(!this->prev);
        assert(!this->next);
        assert(!this->check_);
        s_delete_list.push_back(this);
    }
}

} //namespace co
