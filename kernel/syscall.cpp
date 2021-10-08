#include "syscall.hpp"

#include <fcntl.h>
#include <string.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>

#include "asmfunc.h"
#include "font.hpp"
#include "keyboard.hpp"
#include "logger.hpp"
#include "msr.hpp"
#include "shell.hpp"
#include "system.hpp"
#include "task.hpp"
#include "timer.hpp"

namespace syscall {
struct Result {
    uint64_t value;
    int error;
};

#define SYSCALL(name)                                                       \
    Result name(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, \
                uint64_t arg5, uint64_t arg6)

SYSCALL(LogString) {
    if (arg1 != kError && arg1 != kWarn && arg1 != kInfo && arg1 != kDebug) {
        return {0, EPERM};
    }
    const char *s = reinterpret_cast<const char *>(arg2);
    const auto len = strlen(s);
    if (len > 1024) {
        return {0, E2BIG};
    }
    Log(static_cast<LogLevel>(arg1), "%s", s);
    return {len, 0};
}

SYSCALL(PutString) {
    const auto fd = arg1;
    const char *s = reinterpret_cast<const char *>(arg2);
    const auto len = arg3;
    if (len > 1024) {
        return {0, E2BIG};
    }

    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");

    if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
        return {0, EBADF};
    }
    return {task.Files()[fd]->Write(s, len), 0};
}

SYSCALL(Exit) {
    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");
    return {task.OSStackPointer(), static_cast<int>(arg1)};
}

SYSCALL(GetCurrentTick) { return {timer_manager->CurrentTick(), kTimerFreq}; }

SYSCALL(CreateTimer) {
    const unsigned int mode = arg1;
    const int timer_value = arg2;
    if (timer_value <= 0) {
        return {0, EINVAL};
    }

    __asm__("cli");
    const uint64_t task_id = task_manager->CurrentTask().ID();
    __asm__("sti");

    unsigned long timeout = arg3 * kTimerFreq / 1000;
    if (mode & 1) {  // relative
        timeout += timer_manager->CurrentTick();
    }

    __asm__("cli");
    timer_manager->AddTimer(Timer{timeout, -timer_value, task_id});
    __asm__("sti");
    return {timeout * 1000 / kTimerFreq, 0};
}

namespace {
size_t AllocateFD(Task &task) {
    const size_t num_files = task.Files().size();
    for (size_t i = 0; i < num_files; ++i) {
        if (!task.Files()[i]) {
            return i;
        }
    }
    task.Files().emplace_back();
    return num_files;
}

std::pair<fat::DirectoryEntry *, int> CreateFile(const char *path) {
    auto [file, err] = fat::CreateFile(path);
    switch (err.Cause()) {
        case Error::kIsDirectory:
            return {file, EISDIR};
        case Error::kNoSuchEntry:
            return {file, ENOENT};
        case Error::kNoEnoughMemory:
            return {file, ENOSPC};
        default:
            return {file, 0};
    }
}

}  // namespace

SYSCALL(OpenFile) {
    const char *path = reinterpret_cast<const char *>(arg1);
    const int flags = arg2;
    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");

    if (strcmp(path, "@stdin") == 0) {
        return {0, 0};
    }

    auto [file, post_slash] = fat::FindFile(path);
    if (file == nullptr) {
        if ((flags & O_CREAT) == 0) {
            return {0, ENOENT};
        }
        auto [new_file, err] = CreateFile(path);
        if (err) {
            return {0, err};
        }
        file = new_file;
    } else if (file->attr != fat::Attribute::kDirectory && post_slash) {
        return {0, ENOENT};
    }

    size_t fd = AllocateFD(task);
    task.Files()[fd] = std::make_unique<fat::FileDescriptor>(*file);
    return {fd, 0};
}

SYSCALL(ReadFile) {
    const int fd = arg1;
    void *buf = reinterpret_cast<void *>(arg2);
    size_t count = arg3;
    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");

    if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
        return {0, EBADF};
    }
    return {task.Files()[fd]->Read(buf, count), 0};
}

SYSCALL(DemandPages) {
    const size_t num_pages = arg1;
    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");

    const uint64_t dp_end = task.DPagingEnd();
    task.SetDPagingEnd(dp_end + 4096 * num_pages);
    return {dp_end, 0};
}

SYSCALL(MapFile) {
    const int fd = arg1;
    size_t *file_size = reinterpret_cast<size_t *>(arg2);
    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");

    if (fd < 0 || task.Files().size() <= fd || !task.Files()[fd]) {
        return {0, EBADF};
    }

    *file_size = task.Files()[fd]->Size();
    const uint64_t vaddr_end = task.FileMapEnd();
    const uint64_t vaddr_begin =
        (vaddr_end - *file_size) & 0xffff'ffff'ffff'f000;
    task.SetFileMapEnd(vaddr_begin);
    task.FileMaps().push_back(FileMapping{fd, vaddr_begin, vaddr_end});
    return {vaddr_begin, 0};
}

SYSCALL(NewTask) { return {task_manager->NewTask().ID(), 0}; }

SYSCALL(CopyToTaskBuffer) {
    uint64_t id = arg1;
    void *buf = reinterpret_cast<void *>(arg2);
    size_t offset = arg3;
    size_t len = arg4;
    int remain_bytes = task_manager->CopyToTaskBuffer(id, buf, offset, len);
    if (remain_bytes == -1) {
        return {0, EFBIG};
    }
    size_t res = remain_bytes;
    return {res, 0};
}

SYSCALL(SetArgument) {
    uint64_t id = arg1;
    char *arg = reinterpret_cast<char *>(arg2);

    auto task = task_manager->FindTask(id);

    if (task == nullptr) {
        return {0, ESRCH};
    }
    strcpy(task->arg_, arg);

    return {0, 0};
}

SYSCALL(FindServer) {
    const char *command_line = reinterpret_cast<const char *>(arg1);
    uint64_t task_id = task_manager->FindTask(command_line);
    if (task_id == 0) {
        return {0, ESRCH};
    }
    return {task_id, 0};
}

SYSCALL(OpenReceiveMessage) {
    const auto receive_message = reinterpret_cast<Message *>(arg1);
    const size_t len = arg2;

    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");
    size_t i = 0;

    while (i < len) {
        __asm__("cli");
        auto msg = task.ReceiveMessage();
        if (!msg && i == 0) {
            task.Sleep();
            continue;
        }
        __asm__("sti");

        if (!msg) {
            break;
        }

        receive_message[i].type = msg->type;
        receive_message[i].src_task = msg->src_task;
        receive_message[i].arg = msg->arg;
        ++i;
    }
    return {i, 0};
}

SYSCALL(ClosedReceiveMessage) {
    const auto receive_message = reinterpret_cast<Message *>(arg1);
    const size_t len = arg2;
    uint64_t target_id = arg3;

    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");
    size_t i = 0;

    while (i < len) {
        __asm__("cli");
        auto msg = task.ReceiveMessage();
        if (!msg && i == 0) {
            task.Sleep();
            continue;
        }
        __asm__("sti");

        if (!msg) {
            break;
        }

        if (msg->src_task != target_id) {
            Message emsg;
            emsg.type = Message::Error;
            emsg.arg.error.retry = true;
            emsg.src_task = task.ID();

            __asm__("cli");
            task_manager->SendMessage(msg->src_task, emsg);
            __asm__("sti");
            continue;
        }

        receive_message[i].type = msg->type;
        receive_message[i].src_task = msg->src_task;
        receive_message[i].arg = msg->arg;
        ++i;
    }
    return {i, 0};
}

SYSCALL(SendMessage) {
    const auto send_message = reinterpret_cast<Message *>(arg1);
    uint64_t id = arg2;

    __asm__("cli");
    auto &task = task_manager->CurrentTask();
    __asm__("sti");

    Message msg;
    msg = *send_message;
    msg.src_task = task.ID();

    __asm__("cli");
    task_manager->SendMessage(id, msg);
    __asm__("sti");

    return {0, 0};
}

SYSCALL(WritePixel) {
    const int x = arg1;
    const int y = arg2;
    const uint8_t r = arg3;
    const uint8_t g = arg4;
    const uint8_t b = arg5;
    screen_writer->Write({x, y}, PixelColor{r, g, b});
    return {0, 0};
}

SYSCALL(FrameBufferWitdth) {
    uint64_t w = screen_writer->Width();
    return {w, 0};
}

SYSCALL(FrameBufferHeight) {
    uint64_t h = screen_writer->Height();
    return {h, 0};
}

SYSCALL(CopyToFrameBuffer) {
    const auto src_buf = reinterpret_cast<uint8_t *>(arg1);
    int copy_start_x = arg2;
    int copy_start_y = arg3;
    int bytes_per_copy_line = arg4;
    uint8_t *dst_buf =
        screen->Config().frame_buffer +
        4 * (screen->Config().pixels_per_scan_line * copy_start_y +
             copy_start_x);
    memcpy(dst_buf, src_buf, bytes_per_copy_line);

    return {0, 0};
}

SYSCALL(ReadVolumeImage) {
    void *buf = reinterpret_cast<void *>(arg1);
    size_t offset = arg2;
    size_t len = arg3;

    ReadImage(buf, offset, len);

    return {0, 0};
}

SYSCALL(ReadKernelLog) {
    char *buf = reinterpret_cast<char *>(arg1);
    size_t len = arg2;
    if (klog_changed) {
        size_t remaining = klog_read(buf, len);
        return {remaining, 0};
    } else {
        return {0, EAGAIN};
    }
}

SYSCALL(WriteKernelLog) {
    char *buf = reinterpret_cast<char *>(arg1);
    printk(buf);
    return {0, 0};
}

#undef SYSCALL

}  // namespace syscall

using SyscallFuncType = syscall::Result(uint64_t, uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t);
extern "C" std::array<SyscallFuncType *, 0x17> syscall_table{
    /* 0x00 */ syscall::LogString,
    /* 0x01 */ syscall::PutString,
    /* 0x02 */ syscall::Exit,
    /* 0x03 */ syscall::GetCurrentTick,
    /* 0x04 */ syscall::CreateTimer,
    /* 0x05 */ syscall::OpenFile,
    /* 0x06 */ syscall::ReadFile,
    /* 0x07 */ syscall::DemandPages,
    /* 0x08 */ syscall::MapFile,
    /* 0x09 */ syscall::NewTask,
    /* 0x0a */ syscall::CopyToTaskBuffer,
    /* 0x0b */ syscall::SetArgument,
    /* 0x0c */ syscall::FindServer,
    /* 0x0d */ syscall::OpenReceiveMessage,
    /* 0x0e */ syscall::ClosedReceiveMessage,
    /* 0x0f */ syscall::SendMessage,
    /* 0x10 */ syscall::WritePixel,
    /* 0x11 */ syscall::FrameBufferWitdth,
    /* 0x12 */ syscall::FrameBufferHeight,
    /* 0x13 */ syscall::CopyToFrameBuffer,
    /* 0x14 */ syscall::ReadVolumeImage,
    /* 0x15 */ syscall::ReadKernelLog,
    /* 0x16 */ syscall::WriteKernelLog,

};

void InitializeSyscall() {
    WriteMSR(kIA32_EFER, 0x0501u);
    WriteMSR(kIA32_LSTAR, reinterpret_cast<uint64_t>(SyscallEntry));
    WriteMSR(kIA32_STAR, static_cast<uint64_t>(8) << 32 |
                             static_cast<uint64_t>(16 | 3) << 48);
    WriteMSR(kIA32_FMASK, 0);
}