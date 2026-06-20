use crate::syscall::write;

pub fn print(s: &str) {
    write(1, s.as_bytes());
}

pub fn println(s: &str) {
    write(1, s.as_bytes());
    write(1, b"\n");
}

pub fn print_u64(mut v: u64) {
    if v == 0 {
        write(1, b"0");
        return;
    }
    let mut buf = [0u8; 20];
    let mut i = 20usize;
    while v > 0 {
        i -= 1;
        buf[i] = b'0' + (v % 10) as u8;
        v /= 10;
    }
    write(1, &buf[i..]);
}
