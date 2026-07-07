//! Long-running solve loop at (96,5) so an external sampling profiler
//! (`sample <pid>` on macOS) can attribute time inside the merge.
//! Run:  target/release/req_profile &  ; sample <pid> 5 -f prof.txt
use requihash::*;

fn main() {
    let p = Params::new(96, 5).unwrap();
    let mut nonce = 0u32;
    let mut solves = 0u64;
    loop {
        let eng = Requihash::new(p, b"profile-input", &nonce.to_le_bytes());
        let sols = eng.solve();
        std::hint::black_box(&sols);
        solves += 1;
        nonce = nonce.wrapping_add(1);
        if solves % 20 == 0 {
            eprintln!("profiled {solves} solves");
        }
    }
}
