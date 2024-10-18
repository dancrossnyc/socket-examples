use libc::socketpair;
use libc::{c_int, c_void};
use libc::{cmsghdr, iovec, msghdr, recvmsg, sendmsg};
use libc::{AF_UNIX, SCM_RIGHTS, SOCK_STREAM, SOL_SOCKET};
use libc::{CMSG_DATA, CMSG_FIRSTHDR, CMSG_NXTHDR};
use libc::{MSG_CTRUNC, MSG_TRUNC};

use std::io::Error;

pub type Result<T> = std::result::Result<T, Error>;

#[cfg(not(target_env = "gnu"))]
type MsgLen = libc::socklen_t;
#[cfg(target_env = "gnu")]
type MsgLen = libc::size_t;

#[derive(Clone, Copy, Debug)]
pub struct Sock(c_int);

const fn cmsg_len<T: Sized>() -> MsgLen {
    unsafe { libc::CMSG_LEN(std::mem::size_of::<T>() as u32) as MsgLen }
}

const fn cmsg_space<T: Sized>() -> usize {
    unsafe { libc::CMSG_SPACE(std::mem::size_of::<T>() as u32) as usize }
}

#[repr(C)]
union CMsgInt {
    _align: cmsghdr,
    space: [u8; cmsg_space::<c_int>() ],
}

pub fn socks() -> Result<(Sock, Sock)> {
    let mut sds = [0, 0];
    if unsafe { socketpair(AF_UNIX, SOCK_STREAM, 0, sds.as_mut_ptr()) } < 0 {
        return Err(Error::last_os_error());
    }
    Ok((Sock(sds[0]), Sock(sds[1])))
}

const ZMSGHDR: msghdr = unsafe { std::mem::MaybeUninit::zeroed().assume_init() };

pub fn sendfd(fd: c_int, sd: Sock) -> Result<()> {
    let mut space = std::mem::MaybeUninit::<CMsgInt>::zeroed();
    let space = space.as_mut_ptr();
    let mut dummy = 1u8;
    let mut iov = iovec {
        iov_base: &mut dummy as *mut u8 as *mut c_void,
        iov_len: 1,
    };
    let mh = msghdr {
        msg_iov: &mut iov,
        msg_iovlen: 1,
        msg_control: space.cast(),
        msg_controllen: std::mem::size_of::<CMsgInt>() as MsgLen,
        ..ZMSGHDR
    };
    let cmsg = unsafe { &mut *CMSG_FIRSTHDR(&mh) };
    cmsg.cmsg_len = cmsg_len::<c_int>();
    cmsg.cmsg_level = SOL_SOCKET;
    cmsg.cmsg_type = SCM_RIGHTS;
    unsafe {
        std::ptr::write_unaligned(CMSG_DATA(cmsg).cast(), fd);
    }
    #[cfg(not(miri))]
    if unsafe { sendmsg(sd.0, &mh, 0) } < 0 {
        return Err(Error::last_os_error())
    }
    Ok(())
}

pub fn recvfd(sd: Sock) -> Result<c_int> {
    let mut space = std::mem::MaybeUninit::<CMsgInt>::zeroed();
    let space = space.as_mut_ptr();
    let mut dummy = 0u8;
    let mut iov = iovec {
        iov_base: &mut dummy as *mut u8 as *mut c_void,
        iov_len: 1,
    };
    let mut mh = msghdr {
        msg_iov: &mut iov,
        msg_iovlen: 1,
        msg_control: space.cast(),
        msg_controllen: std::mem::size_of::<CMsgInt>() as MsgLen,
        ..ZMSGHDR
    };
    #[cfg(not(miri))]
    if unsafe { recvmsg(sd.0, &mut mh, 0) } < 0 {
        return Err(Error::last_os_error());
    }
    if mh.msg_flags & (MSG_TRUNC | MSG_CTRUNC) != 0 {
        return Err(Error::other("recvfd: message truncated"));
    }
    let cmsg = unsafe { &mut *CMSG_FIRSTHDR(&mh) };
    if cmsg.cmsg_len != cmsg_len::<c_int>() {
        return Err(Error::other("recvfd: wrong control message size"));
    }
    if cmsg.cmsg_level != SOL_SOCKET || cmsg.cmsg_type != SCM_RIGHTS {
        return Err(Error::other("recvfd: bad message metadata"));
    }
    if dummy != 1 {
        return Err(Error::other("recvfd: read unexpected message data"));
    }
    let next_hdr = unsafe { CMSG_NXTHDR(&mh, cmsg) };
    if !next_hdr.is_null() {
        return Err(Error::other("recvfd: extra control message"));
    }
    Ok(unsafe { std::ptr::read_unaligned(CMSG_DATA(cmsg).cast()) })
}

fn main() {
    let (snd, rcv) = socks().expect("socketpair");
    sendfd(1, snd).expect("sendfd");
    let n = recvfd(rcv).expect("recvfd");
    println!("snd={snd:?}, rcv={rcv:?}, n={n}");
    let msg = "Hello, World!\n";
    unsafe {
        libc::write(n, msg.as_ptr().cast(), msg.len());
    }
}
