//! Driver: `cs_gen <n> <k> <nonce_hex>` runs the k-list solver and prints
//! one JSON line matching the C++ `cs_gen`'s schema
//! `{"n":..,"k":..,"nonce_hex":"..","solutions":[[..],..],"verified":bool}`
//! so a comparison tool can treat both uniformly. Harness only.

use cs_rs::KListWagnerAlgorithm;

fn hex_to_bytes(hex: &str) -> Vec<u8> {
    (0..hex.len() / 2)
        .map(|i| u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16).expect("valid hex"))
        .collect()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 4 {
        eprintln!("usage: {} <n> <k> <nonce_hex (32 hex chars = 16 bytes)>", args[0]);
        std::process::exit(1);
    }
    let n: u32 = args[1].parse().expect("n");
    let k: u32 = args[2].parse().expect("k");
    let nonce = hex_to_bytes(&args[3]);

    let solver = KListWagnerAlgorithm::new(n, k, nonce).expect("valid params");
    let solutions = solver.solve();
    let verified = solver.verify(&solutions);

    let sols_json: Vec<String> = solutions
        .iter()
        .map(|s| {
            let idxs: Vec<String> = s.iter().map(|i| i.to_string()).collect();
            format!("[{}]", idxs.join(","))
        })
        .collect();
    println!(
        "{{\"n\":{n},\"k\":{k},\"nonce_hex\":\"{}\",\"solutions\":[{}],\"verified\":{verified}}}",
        args[3],
        sols_json.join(",")
    );
}
