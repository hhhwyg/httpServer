import re

with open("/home/wyg/WebServer/WebServer/IoUring.cpp", "r") as f:
    content = f.read()

# We want to replace the `poll` and `getEventsRequest` methods with `processEvents`,
# and update `submitPollAdd` and `cancelPoll` to use the packed user_data.
# Also add `submitRead` and `submitWrite`.

new_content = content

# 1. Update user_data packing and unpacking functions
helpers = """
// 辅助函数：将 fd 和操作类型打包到 64 位 user_data 中
static uint64_t packUserData(int fd, UringOp op) {
    return ((uint64_t)op << 32) | (uint32_t)fd;
}

static void unpackUserData(uint64_t data, int& fd, UringOp& op) {
    fd = (int)(data & 0xFFFFFFFF);
    op = (UringOp)(data >> 32);
}

"""

new_content = new_content.replace("IoUring::IoUring() {", helpers + "IoUring::IoUring() {")

# 2. Update submitPollAdd to use packUserData
new_content = new_content.replace(
    "io_uring_sqe_set_data64(sqe, (uint64_t)fd);",
    "io_uring_sqe_set_data64(sqe, packUserData(fd, UringOp::POLL_ADD));"
)

# 3. Update cancelPoll to use -1
# Actually cancel_poll uses io_uring_prep_poll_remove. Does poll_remove take the user_data?
# Yes, it removes by user_data! So we need to remove `packUserData(fd, UringOp::POLL_ADD)`.
# Wait! Let's check `io_uring_prep_poll_remove`. Yes, it removes by user_data.
new_content = new_content.replace(
    "io_uring_prep_poll_remove(sqe, (uint64_t)fd);",
    "io_uring_prep_poll_remove(sqe, packUserData(fd, UringOp::POLL_ADD));"
)

# 4. Remove `poll` and `getEventsRequest`, add `processEvents`, `submitRead`, `submitWrite`
remove_regex = re.compile(r"std::vector<SP_Channel> IoUring::poll\(\) \{.*\}  return req_data;\n\}\n", re.DOTALL)
process_events_code = """
void IoUring::submitRead(SP_Channel request, void* buffer, size_t len) {
    int fd = request->getFd();
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
        if (!sqe) return;
    }
    
    io_uring_prep_read(sqe, fd, buffer, len, 0);
    io_uring_sqe_set_data64(sqe, packUserData(fd, UringOp::ASYNC_READ));
}

void IoUring::submitWrite(SP_Channel request, const void* buffer, size_t len) {
    int fd = request->getFd();
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
        if (!sqe) return;
    }
    
    io_uring_prep_write(sqe, fd, buffer, len, 0);
    io_uring_sqe_set_data64(sqe, packUserData(fd, UringOp::ASYNC_WRITE));
}

void IoUring::processEvents() {
    while (true) {
        io_uring_submit(&ring_);

        struct __kernel_timespec ts;
        ts.tv_sec = 10;
        ts.tv_nsec = 0;

        struct io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);

        if (ret < 0) {
            if (ret == -EINTR) continue;
            if (ret == -ETIME) return; // timeout
            LOG << "io_uring_wait_cqe_timeout error: " << strerror(-ret);
            return;
        }

        struct io_uring_cqe* cqes[4096];
        int count = io_uring_peek_batch_cqe(&ring_, cqes, 4096);

        if (count > 0) {
            for (int i = 0; i < count; ++i) {
                uint64_t data = io_uring_cqe_get_data64(cqes[i]);
                if (data == (uint64_t)-1) continue;
                
                int fd;
                UringOp op;
                unpackUserData(data, fd, op);

                if (cqes[i]->res < 0 && cqes[i]->res != -ECANCELED) {
                    // LOG error
                }

                if (fd >= 0 && fd < MAXFDS) {
                    SP_Channel cur_req = fd2chan_[fd];
                    if (cur_req) {
                        if (op == UringOp::POLL_ADD) {
                            __uint32_t epoll_events = pollToEpollEvents(cqes[i]->res);
                            cur_req->setRevents(epoll_events);
                            cur_req->setEvents(0);
                            cur_req->handleEvents();
                        } else if (op == UringOp::ASYNC_READ) {
                            cur_req->handleReadComplete(cqes[i]->res);
                        } else if (op == UringOp::ASYNC_WRITE) {
                            cur_req->handleWriteComplete(cqes[i]->res);
                        }
                    }
                }
            }
            io_uring_cq_advance(&ring_, count);
            return;
        }
    }
}
"""

# Wait, `getEventsRequest` also needs to be removed.
# I'll just use string replacement carefully.
part1 = content.split("std::vector<SP_Channel> IoUring::poll() {")[0]
part2 = content.split("std::vector<SP_Channel> IoUring::getEventsRequest(")[1]
part3 = part2.split("void IoUring::add_timer")[1] # anything after getEventsRequest

final_content = part1 + process_events_code + "\nvoid IoUring::add_timer" + part3

# apply the pack changes to final_content
final_content = final_content.replace("IoUring::IoUring() {", helpers + "IoUring::IoUring() {")
final_content = final_content.replace(
    "io_uring_sqe_set_data64(sqe, (uint64_t)fd);",
    "io_uring_sqe_set_data64(sqe, packUserData(fd, UringOp::POLL_ADD));"
)
final_content = final_content.replace(
    "io_uring_prep_poll_remove(sqe, (uint64_t)fd);",
    "io_uring_prep_poll_remove(sqe, packUserData(fd, UringOp::POLL_ADD));"
)

with open("/home/wyg/WebServer/WebServer/IoUring.cpp", "w") as f:
    f.write(final_content)

print("Done")
