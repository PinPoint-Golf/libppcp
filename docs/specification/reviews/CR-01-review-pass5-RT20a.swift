// RT-20a — the interposition property as arithmetic (PPCP-RV, erratum E50).
// Four keypairs per trial: initiator, acceptor, and the two an interposer holds.
// Both legs' digits computed through 11.6c. Assert they differ.
import CryptoKit
import Foundation

@inline(__always)
func sas(_ z: SharedSecret, _ v: UInt8, _ pkI: Data, _ pkA: Data) -> UInt32 {
    let bk = HKDF<SHA256>.extract(
        inputKeyMaterial: SymmetricKey(data: z.withUnsafeBytes { Data($0) }),
        salt: Data("ppcp1 bootstrap".utf8))
    var info = Data("ppcp1 sas".utf8); info.append(v); info.append(pkI); info.append(pkA)
    let raw = HKDF<SHA256>.expand(pseudoRandomKey: bk, info: info, outputByteCount: 4)
    return raw.withUnsafeBytes { $0.load(as: UInt32.self).bigEndian } % 1_000_000
}

let trials = Int(CommandLine.arguments.dropFirst().first ?? "") ?? 200_000
var collisions = 0
var histogram = [Int](repeating: 0, count: 1000)   // top three digits, for uniformity

let start = Date()
for _ in 0..<trials {
    let skI = Curve25519.KeyAgreement.PrivateKey()
    let skA = Curve25519.KeyAgreement.PrivateKey()
    let skM1 = Curve25519.KeyAgreement.PrivateKey()   // interposer, initiator's leg
    let skM2 = Curve25519.KeyAgreement.PrivateKey()   // interposer, acceptor's leg
    let pkI = skI.publicKey.rawRepresentation
    let pkA = skA.publicKey.rawRepresentation
    let pkM1 = skM1.publicKey.rawRepresentation
    let pkM2 = skM2.publicKey.rawRepresentation

    // Leg 1: the initiator believes it is talking to M1.
    let leg1 = sas(try! skI.sharedSecretFromKeyAgreement(with: skM1.publicKey), 1, pkI, pkM1)
    // Leg 2: the acceptor believes M2 is the initiator.
    let leg2 = sas(try! skM2.sharedSecretFromKeyAgreement(with: skA.publicKey), 1, pkM2, pkA)

    if leg1 == leg2 { collisions += 1 }
    histogram[Int(leg1) / 1000] += 1
}
let elapsed = Date().timeIntervalSince(start)

// Chi-square over the top three digits, 1000 buckets, 999 dof.
let expected = Double(trials) / 1000.0
let chi2 = histogram.reduce(0.0) { $0 + pow(Double($1) - expected, 2) / expected }

print("""

RT-20a — interposition as arithmetic
  trials                 \(trials)   (\(String(format: "%.1f", elapsed)) s, \(Int(Double(trials)/elapsed))/s)
  legs colliding         \(collisions)
  expected at 1.0e-6     \(String(format: "%.3f", Double(trials) * 1.0e-6))
  expected at 2^-20      \(String(format: "%.3f", Double(trials) * 9.5367431640625e-07))
  uniformity chi2        \(String(format: "%.1f", chi2))  over 999 dof  (expect ~999 +/- 45)
""")
