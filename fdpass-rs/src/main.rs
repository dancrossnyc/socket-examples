use libc::socketpair;
use libc::{c_int, c_void};
use libc::{iovec, msghdr, recvmsg, sendmsg};
use libc::{AF_UNIX, SCM_RIGHTS, SOCK_SEQPACKET, SOL_SOCKET};
use libc::{CMSG_DATA, CMSG_FIRSTHDR, CMSG_LEN, CMSG_NXTHDR, CMSG_SPACE};

#[cfg(target_env = "gnu")]
type MsgLen = libc::size_t;
#[cfg(not(target_env = "gnu"))]
type MsgLen = libc::c_uint;

#[derive(Clone, Copy, Debug)]
pub struct Sock(c_int);

pub fn socks() -> (Sock, Sock) {
    let mut sds = [0, 0];
    unsafe {
        if socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sds.as_mut_ptr()) < 0 {
            panic!("socketpair failed: {}", std::io::Error::last_os_error());
        }
    }
    (Sock(sds[0]), Sock(sds[1]))
}

const C_INT_SIZE: u32 = std::mem::size_of::<c_int>() as u32;

pub fn sendfd(fd: c_int, sd: Sock) {
    let mut space = vec![0u8; unsafe { CMSG_SPACE(C_INT_SIZE) } as usize];
    let space_len = space.len();
    let mut dummy = 1u8;
    let mut iov = iovec {
        iov_base: &raw mut dummy as *mut c_void,
        iov_len: 1,
    };
    let zmh: msghdr = unsafe { std::mem::zeroed() };
    let mut mh = msghdr {
        msg_iov: &mut iov,
        msg_iovlen: 1,
        msg_control: space.as_mut_ptr() as *mut c_void,
        msg_controllen: space_len as MsgLen,
        ..zmh
    };
    let cmsg = unsafe { &mut *CMSG_FIRSTHDR(&mut mh) };
    cmsg.cmsg_len = unsafe { CMSG_LEN(C_INT_SIZE) } as MsgLen;
    cmsg.cmsg_level = SOL_SOCKET;
    cmsg.cmsg_type = SCM_RIGHTS;
    unsafe {
        let dp = CMSG_DATA(cmsg) as *mut c_int;
        std::ptr::write(dp, fd);
        if sendmsg(sd.0, &mh, 0) < 0 {
            panic!("sendmsg failed: {}", std::io::Error::last_os_error())
        }
    }
}

pub fn recvfd(sd: Sock) -> c_int {
    let mut space = vec![0u8; unsafe { CMSG_SPACE(C_INT_SIZE) } as usize];
    let space_len = space.len();
    let mut dummy = 0u8;
    let mut iov = iovec {
        iov_base: &raw mut dummy as *mut c_void,
        iov_len: 1,
    };
    let zmh: msghdr = unsafe { std::mem::zeroed() };
    let mut mh = msghdr {
        msg_iov: &mut iov,
        msg_iovlen: 1,
        msg_control: space.as_mut_ptr() as *mut c_void,
        msg_controllen: space_len as MsgLen,
        ..zmh
    };
    let cmsg = unsafe { &mut *CMSG_FIRSTHDR(&mut mh) };
    cmsg.cmsg_len = unsafe { CMSG_LEN(C_INT_SIZE) } as MsgLen;
    cmsg.cmsg_level = SOL_SOCKET;
    cmsg.cmsg_type = SCM_RIGHTS;
    unsafe {
        if recvmsg(sd.0, &mut mh, 0) < 0 {
            panic!("sendmsg failed: {}", std::io::Error::last_os_error());
        }
    }
    let next_hdr = unsafe { CMSG_NXTHDR(&mh, cmsg) };
    if !next_hdr.is_null() {
        panic!("bad message");
    }
    if dummy != 1 {
        panic!("read bad data");
    }
    unsafe {
        let dp = CMSG_DATA(cmsg) as *mut c_int;
        std::ptr::read(dp)
    }
}

fn main() {
    let (snd, rcv) = socks();
    sendfd(1, snd);
    let n = recvfd(rcv);
    println!("snd={snd:?}, rcv={rcv:?}, n={n}");
    let msg = "Hello, World!\n";
    unsafe { libc::write(n, msg.as_ptr() as *const c_void, msg.len()); } 
}
