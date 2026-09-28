#!/usr/bin/env python3
"""test_pubkey's vectors, made by Python's cryptography (OpenSSL), never by
the kit: two RSA keys with signatures (PKCS#1 v1.5, PSS) and envelopes
(v1.5, OAEP) by them, and an ECDSA key and signatures on each of the six
curves the kit checks.

    python3 gen_pubkey.py tests/fixtures/pubkey.inc

Needs cryptography (pip install cryptography).  Every run makes new keys,
so the fixture changes whole; the suite checks what it holds, not which
keys.
"""
import sys, hashlib
from cryptography.hazmat.primitives.asymmetric import rsa, ec, padding, utils
from cryptography.hazmat.primitives import hashes, serialization

out = []
def arr(name, b):
    out.append("static const unsigned char %s[%d] = {" % (name, len(b)))
    for i in range(0, len(b), 12):
        out.append("    " + ", ".join("0x%02x" % x for x in b[i:i+12]) + ",")
    out.append("};")
def num(v, n=None):
    n = n or (v.bit_length() + 7) // 8
    return v.to_bytes(n, 'big')

msg = b"TikuOS reads what was signed and sealed, and checks it."
out.append("/* test_pubkey's vectors, made by Python's cryptography %s (OpenSSL)"
           % __import__('cryptography').__version__)
out.append(" * and never by the kit: two RSA keys, signatures and envelopes by them,")
out.append(" * and an ECDSA key and signature on each curve.  The message signed is")
out.append(" * pk_message; the sealed one is pk_secret.")
out.append(" * SPDX-License-Identifier: Apache-2.0 */")
arr("pk_message", msg)
secret = bytes(range(1, 25))
arr("pk_secret", secret)

def rsa_key(bits, tag):
    k = rsa.generate_private_key(public_exponent=65537, key_size=bits)
    pn = k.private_numbers(); pub = pn.public_numbers
    nb = (bits + 7) // 8
    arr(tag + "_n", num(pub.n, nb)); arr(tag + "_e", num(pub.e))
    arr(tag + "_d", num(pn.d, nb)); arr(tag + "_p", num(pn.p)); arr(tag + "_q", num(pn.q))
    arr(tag + "_dp", num(pn.dmp1)); arr(tag + "_dq", num(pn.dmq1)); arr(tag + "_qi", num(pn.iqmp))
    return k

k2 = rsa_key(2048, "rsa2k")
H = {'sha1': hashes.SHA1(), 'sha256': hashes.SHA256(), 'sha384': hashes.SHA384(), 'sha512': hashes.SHA512()}
for h in ('sha1', 'sha256', 'sha512'):
    arr("rsa2k_v15_" + h, k2.sign(msg, padding.PKCS1v15(), H[h]))
arr("rsa2k_pss_sha256", k2.sign(msg, padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32), hashes.SHA256()))
arr("rsa2k_pss_max", k2.sign(msg, padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=padding.PSS.MAX_LENGTH), hashes.SHA256()))
arr("rsa2k_pss_sha512", k2.sign(msg, padding.PSS(mgf=padding.MGF1(hashes.SHA1()), salt_length=20), hashes.SHA512()))
pub2 = k2.public_key()
arr("rsa2k_env_v15", pub2.encrypt(secret, padding.PKCS1v15()))
arr("rsa2k_env_oaep1", pub2.encrypt(secret, padding.OAEP(mgf=padding.MGF1(hashes.SHA1()), algorithm=hashes.SHA1(), label=None)))
arr("rsa2k_env_oaep256", pub2.encrypt(secret, padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=None)))

k1 = rsa_key(1024, "rsa1k")
arr("rsa1k_v15_sha256", k1.sign(msg, padding.PKCS1v15(), hashes.SHA256()))
arr("rsa1k_env_v15", k1.public_key().encrypt(secret, padding.PKCS1v15()))

curves = [("p256", ec.SECP256R1(), hashes.SHA256()), ("p384", ec.SECP384R1(), hashes.SHA384()),
          ("p521", ec.SECP521R1(), hashes.SHA512()), ("bp256", ec.BrainpoolP256R1(), hashes.SHA256()),
          ("bp384", ec.BrainpoolP384R1(), hashes.SHA384()), ("bp512", ec.BrainpoolP512R1(), hashes.SHA512())]
for tag, curve, h in curves:
    k = ec.generate_private_key(curve)
    pub = k.public_key()
    arr("ec_%s_q" % tag, pub.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint))
    arr("ec_%s_qc" % tag, pub.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.CompressedPoint))
    r, s = utils.decode_dss_signature(k.sign(msg, ec.ECDSA(h)))
    arr("ec_%s_r" % tag, num(r)); arr("ec_%s_s" % tag, num(s))
    # the same key over SHA-1, whose digest is shorter than the order,
    # and over SHA-512, longer than all but P-521's
    r, s = utils.decode_dss_signature(k.sign(msg, ec.ECDSA(hashes.SHA1())))
    arr("ec_%s_r1" % tag, num(r)); arr("ec_%s_s1" % tag, num(s))
    r, s = utils.decode_dss_signature(k.sign(msg, ec.ECDSA(hashes.SHA512())))
    arr("ec_%s_r5" % tag, num(r)); arr("ec_%s_s5" % tag, num(s))
open(sys.argv[1], "w").write("\n".join(out) + "\n")
