#!/usr/bin/env python3
"""Key files, envelopes, signed data and the PDFs holding them, made by
Python's cryptography, the OpenSSL (or LibreSSL) command line and pyHanko:
never by the kit.  Writes three fixtures side by side:

    python3 gen_cms.py SCRATCH tests/fixtures/cms.inc

  cms.inc     test_cms: PKCS#12 and PEM key files, CMS envelopes in every
              content cipher, signatures, an RFC 3161 timestamp token
  pubsec.inc  test_pdf_crypt: PDFs pyHanko sealed to certificates
  pdfsig.inc  test_pdf_sign: PDFs pyHanko signed, and the two old SHA-1
              forms assembled here around signatures made elsewhere

SCRATCH takes the keys and documents as files; poppler's pdfsig, when on
the path, is shown each signed document and its verdict noted.  Needs
cryptography, pyhanko and the openssl command.  Every run makes new keys,
so all three change together.
"""
import sys, os, subprocess, datetime, hashlib, warnings
warnings.filterwarnings("ignore")
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa, ec, padding
from cryptography.hazmat.primitives.serialization import pkcs7, pkcs12
from cryptography.hazmat.primitives.serialization.pkcs12 import PBES

W = sys.argv[1]            # a scratch folder
OUT = sys.argv[2]          # the .inc
os.makedirs(W, exist_ok=True)
out = []
import base64
def arr(name, b):
    t = base64.b64encode(b).decode()
    out.append("static const char %s[] = /* %d bytes */" % (name, len(b)))
    for i in range(0, len(t), 72):
        out.append('    "%s"%s' % (t[i:i+72], ";" if i + 72 >= len(t) else ""))
def ossl(*args, inp=None):
    return subprocess.run(["openssl"] + list(args), input=inp, check=True,
                          capture_output=True).stdout
class _NoPdfsig:
    stdout = ""
def pdfsig(path):
    """poppler's verdicts on @path, or none when pdfsig is not installed."""
    try:
        return subprocess.run(["pdfsig", path], capture_output=True, text=True)
    except FileNotFoundError:
        return _NoPdfsig()

def w(name, data):
    p = os.path.join(W, name); open(p, "wb").write(data); return p

when = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)
def cert_for(key, cn, org, issuer_key=None, issuer_name=None, serial=1001):
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn),
                      x509.NameAttribute(NameOID.ORGANIZATION_NAME, org)])
    b = (x509.CertificateBuilder().subject_name(name)
         .issuer_name(issuer_name or name).public_key(key.public_key())
         .serial_number(serial).not_valid_before(when)
         .not_valid_after(when + datetime.timedelta(days=3650))
         .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), False))
    if cn.endswith("TSA"):
        b = b.add_extension(x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.TIME_STAMPING]), True)
    else:
        rsa_key = isinstance(key, rsa.RSAPrivateKey)
        b = b.add_extension(x509.KeyUsage(digital_signature=True, content_commitment=True,
                                          key_encipherment=rsa_key, data_encipherment=False,
                                          key_agreement=not rsa_key, key_cert_sign=False,
                                          crl_sign=False, encipher_only=False,
                                          decipher_only=False), True)
    return b.sign(issuer_key or key, hashes.SHA256())

key = rsa.generate_private_key(public_exponent=65537, key_size=1024)
crt = cert_for(key, "Tiku Test Signer", "TikuOS Tests")
other = rsa.generate_private_key(public_exponent=65537, key_size=1024)
other_crt = cert_for(other, "Someone Else", "Elsewhere", serial=2002)
eck = ec.generate_private_key(ec.SECP256R1())
ec_crt = cert_for(eck, "Tiku EC Signer", "TikuOS Tests", serial=3003)
tsak = rsa.generate_private_key(public_exponent=65537, key_size=1024)
tsa_crt = cert_for(tsak, "Tiku Test TSA", "TikuOS Tests", serial=4004)

pem = serialization.Encoding.PEM
key_pem = w("key.pem", key.private_bytes(pem, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
crt_pem = w("crt.pem", crt.public_bytes(pem))
w("other.pem", other_crt.public_bytes(pem))
w("eckey.pem", eck.private_bytes(pem, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
w("eccrt.pem", ec_crt.public_bytes(pem))

out.append("/* test_cms's key files, envelopes and signatures, made by Python's")
out.append(" * cryptography, the LibreSSL command line and pyHanko, never the kit:")
out.append(" * each fixture's comment says which, and how.  The key is Tiku Test")
out.append(" * Signer's (RSA 1024).  Each is base64; ck_message is what is signed.")
out.append(" * SPDX-License-Identifier: Apache-2.0 */")
msg = b"The first twenty bytes, and four more: a PDF's seed, and its P."
arr("ck_message", msg)
arr("ck_cert", crt.public_bytes(serialization.Encoding.DER))
arr("ck_ec_cert", ec_crt.public_bytes(serialization.Encoding.DER))

# ---- key files
enc = (serialization.PrivateFormat.PKCS12.encryption_builder()
       .kdf_rounds(2048).key_cert_algorithm(PBES.PBESv2SHA256AndAES256CBC)
       .hmac_hash(hashes.SHA256()).build("pässwörd".encode()))
out.append("/* cryptography: PBES2, AES-256 and PBKDF2-SHA256; HMAC-SHA256; pässwörd */")
arr("ck_p12_modern", pkcs12.serialize_key_and_certificates(b"tiku", key, crt, None, enc))
enc = (serialization.PrivateFormat.PKCS12.encryption_builder()
       .kdf_rounds(2048).key_cert_algorithm(PBES.PBESv1SHA1And3KeyTripleDESCBC)
       .hmac_hash(hashes.SHA1()).build(b"secret"))
out.append("/* cryptography: PKCS#12's triple DES scheme, HMAC-SHA1; secret */")
arr("ck_p12_legacy", pkcs12.serialize_key_and_certificates(b"tiku", key, crt, [other_crt], enc))
out.append("/* LibreSSL pkcs12 -export: triple DES key, RC2-40 certificates; secret */")
arr("ck_p12_libressl", ossl("pkcs12", "-export", "-inkey", key_pem, "-in", crt_pem, "-passout", "pass:secret"))
out.append("/* LibreSSL pkcs12 -export, the empty password */")
arr("ck_p12_empty", ossl("pkcs12", "-export", "-inkey", key_pem, "-in", crt_pem, "-passout", "pass:"))
out.append("/* cryptography: PKCS#8, and the certificate, as PEM */")
arr("ck_pem_plain", key.private_bytes(pem, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()) + crt.public_bytes(pem))
out.append("/* cryptography: PKCS#8 sealed by PBES2 (its best), as PEM; secret */")
arr("ck_pem_sealed", key.private_bytes(pem, serialization.PrivateFormat.PKCS8, serialization.BestAvailableEncryption(b"secret")))
out.append("/* cryptography: PKCS#1, as PEM */")
arr("ck_pem_rsa", key.private_bytes(pem, serialization.PrivateFormat.TraditionalOpenSSL, serialization.NoEncryption()))
out.append("/* cryptography: an EC key, as PEM (not one an envelope opens) */")
arr("ck_pem_ec", eck.private_bytes(pem, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
out.append("/* cryptography: PKCS#1 under OpenSSL's old sealing; secret */")
arr("ck_pem_old", key.private_bytes(pem, serialization.PrivateFormat.TraditionalOpenSSL, serialization.BestAvailableEncryption(b"secret")))

# ---- envelopes
mf = w("msg.bin", msg)
for tag, flag in (("aes256", "-aes256"), ("aes128", "-aes128"), ("aes192", "-aes192"),
                  ("des3", "-des3"), ("rc2", "-rc2-40"), ("rc2_128", "-rc2-128")):
    out.append("/* LibreSSL cms -encrypt %s, to the signer */" % flag)
    arr("ck_env_" + tag, ossl("cms", "-encrypt", "-binary", "-outform", "DER", flag, "-in", mf, crt_pem))
out.append("/* LibreSSL cms -encrypt -aes256 to someone else, then to both */")
arr("ck_env_elsewhere", ossl("cms", "-encrypt", "-binary", "-outform", "DER", "-aes256", "-in", mf, os.path.join(W, "other.pem")))
arr("ck_env_both", ossl("cms", "-encrypt", "-binary", "-outform", "DER", "-aes256", "-in", mf, os.path.join(W, "other.pem"), crt_pem))
out.append("/* cryptography: an envelope, AES-128, v1.5 */")
arr("ck_env_pyca", pkcs7.PKCS7EnvelopeBuilder().set_data(msg).add_recipient(crt)
    .set_content_encryption_algorithm(pkcs7.algorithms.AES128)
    .encrypt(serialization.Encoding.DER, [pkcs7.PKCS7Options.Binary]))
try:
    out.append("/* LibreSSL cms -encrypt -aes256, the key sealed by RSA-OAEP */")
    arr("ck_env_oaep", ossl("cms", "-encrypt", "-binary", "-outform", "DER", "-aes256", "-in", mf,
                            "-recip", crt_pem, "-keyopt", "rsa_padding_mode:oaep"))
except subprocess.CalledProcessError as e:
    out.pop(); print("no OAEP envelope:", e.stderr.decode()[:200])

# ---- signatures
def sign(k, c, h, opts, rsa_pad=None):
    b = pkcs7.PKCS7SignatureBuilder().set_data(msg).add_signer(c, k, h, rsa_padding=rsa_pad) if rsa_pad else \
        pkcs7.PKCS7SignatureBuilder().set_data(msg).add_signer(c, k, h)
    return b.sign(serialization.Encoding.DER, opts)
D, B, N = pkcs7.PKCS7Options.DetachedSignature, pkcs7.PKCS7Options.Binary, pkcs7.PKCS7Options.NoAttributes
out.append("/* LibreSSL cms -sign, detached, SHA-256 with attributes */")
arr("ck_sig_rsa", ossl("cms", "-sign", "-binary", "-outform", "DER", "-md", "sha256", "-signer", crt_pem, "-inkey", key_pem, "-in", mf))
out.append("/* LibreSSL cms -sign -noattr, detached, SHA-1 */")
arr("ck_sig_noattr", ossl("cms", "-sign", "-binary", "-noattr", "-outform", "DER", "-md", "sha1", "-signer", crt_pem, "-inkey", key_pem, "-in", mf))
out.append("/* LibreSSL cms -sign -nodetach: the content inside */")
arr("ck_sig_inside", ossl("cms", "-sign", "-binary", "-nodetach", "-outform", "DER", "-md", "sha256", "-signer", crt_pem, "-inkey", key_pem, "-in", mf))
out.append("/* LibreSSL cms -sign -nocerts: no certificate to check it by */")
arr("ck_sig_nocert", ossl("cms", "-sign", "-binary", "-nocerts", "-outform", "DER", "-signer", crt_pem, "-inkey", key_pem, "-in", mf))
out.append("/* cryptography: ECDSA P-256, SHA-384, detached */")
arr("ck_sig_ec", sign(eck, ec_crt, hashes.SHA384(), [D, B]))
out.append("/* cryptography: RSASSA-PSS, SHA-256, its salt as long, detached */")
arr("ck_sig_pss", sign(key, crt, hashes.SHA256(), [D, B],
                       padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=padding.PSS.DIGEST_LENGTH)))

# ---- a timestamp token over the message, from pyHanko's test authority
from asn1crypto import x509 as ax, keys as ak
from pyhanko.sign.timestamps.dummy_client import DummyTimeStamper
import asyncio, inspect
st = DummyTimeStamper(tsa_cert=ax.Certificate.load(tsa_crt.public_bytes(serialization.Encoding.DER)),
                      tsa_key=ak.PrivateKeyInfo.load(tsak.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())),
                      fixed_dt=datetime.datetime(2026, 9, 28, 12, 34, 56, tzinfo=datetime.timezone.utc))
digest = hashlib.sha256(msg).digest()
fn = getattr(st, "async_timestamp", None) or getattr(st, "timestamp")
tok = fn(digest, "sha256")
if inspect.iscoroutine(tok): tok = asyncio.get_event_loop().run_until_complete(tok)
out.append("/* pyHanko's test authority: an RFC 3161 token over SHA-256 of ck_message,")
out.append(" * dated 2026-09-28 12:34:56 UTC */")
arr("ck_stamp", tok.dump())
open(OUT, "w").write("\n".join(out) + "\n")
print("ok", len(out))

# ---- documents sealed to certificates, by pyHanko: tests/fixtures/pubsec.inc
from io import BytesIO
from pyhanko.pdf_utils.reader import PdfFileReader
from pyhanko.pdf_utils.writer import copy_into_new_writer
from pyhanko.pdf_utils.crypt import PubKeySecurityHandler, SecurityHandlerVersion
from pyhanko.pdf_utils.crypt.pubkey import RecipientEncryptionPolicy
from pyhanko.pdf_utils.crypt.permissions import PubKeyPermissions

def plain_pdf(text):
    objs = [b"<< /Type /Catalog /Pages 2 0 R >>",
            b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Contents 4 0 R "
            b"/Resources << /Font << /F1 5 0 R >> >> >>",
            None,
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            b"<< /Title (" + text + b") >>"]
    body = b"BT /F1 18 Tf 20 100 Td (" + text + b") Tj ET"
    objs[3] = b"<< /Length %d >>\nstream\n" % len(body) + body + b"\nendstream"
    out = b"%PDF-1.7\n"; offs = []
    for i, o in enumerate(objs):
        offs.append(len(out)); out += b"%d 0 obj\n" % (i + 1) + o + b"\nendobj\n"
    x = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    for o in offs: out += b"%010d 00000 n \n" % o
    out += b"trailer\n<< /Size %d /Root 1 0 R /Info 6 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)
    return out

acrt = lambda c: ax.Certificate.load(c.public_bytes(serialization.Encoding.DER))
def sealed(certs, **kw):
    r = PdfFileReader(BytesIO(plain_pdf(b"Sealed to a certificate")))
    wr = copy_into_new_writer(r)
    sh = PubKeySecurityHandler.build_from_certs([acrt(c) for c in certs], **kw)
    wr._assign_security_handler(sh) if hasattr(wr, "_assign_security_handler") else setattr(wr, "security_handler", sh)
    if not hasattr(wr, "_assign_security_handler"):
        wr._encrypt = wr.add_object(sh.as_pdf_object())
    buf = BytesIO(); wr.write(buf); return buf.getvalue()

main_out = out
out = []
out.append("/* test_pdf_crypt's documents sealed to certificates, by pyHanko %s," % __import__('pyhanko').__version__ if hasattr(__import__('pyhanko'), '__version__') else "/* test_pdf_crypt's documents sealed to certificates, by pyHanko,")
out.append(" * each one page saying \"Sealed to a certificate\", to Tiku Test Signer")
out.append(" * (cms.inc's key) unless its comment says otherwise.  Base64.")
out.append(" * SPDX-License-Identifier: Apache-2.0 */")
noex = PubKeyPermissions.allow_everything() & ~PubKeyPermissions.ALLOW_CONTENT_EXTRACTION
cases = [
    ("ps_aes256", "V 5, AES-256, /adbe.pkcs7.s5, with pyHanko's PDF MAC", [crt], {}),
    ("ps_aes128", "V 4, AES-128 crypt filter", [crt], dict(version=SecurityHandlerVersion.RC4_OR_AES128, pdf_mac=False)),
    ("ps_rc4cf", "V 4, an RC4 crypt filter, 128 bits", [crt], dict(version=SecurityHandlerVersion.RC4_OR_AES128, use_aes=False, pdf_mac=False)),
    ("ps_rc4", "V 2, RC4 128 bits, no crypt filters", [crt], dict(version=SecurityHandlerVersion.RC4_LONGER_KEYS, use_aes=False, use_crypt_filters=False, pdf_mac=False)),
    ("ps_rc4_40", "V 1, RC4 40 bits", [crt], dict(version=SecurityHandlerVersion.RC4_40, keylen_bytes=5, use_aes=False, use_crypt_filters=False, pdf_mac=False)),
    ("ps_oaep", "V 5, its envelope key sealed by RSA-OAEP", [crt], dict(policy=RecipientEncryptionPolicy(prefer_oaep=True), pdf_mac=False)),
    ("ps_meta", "V 5, metadata left clear", [crt], dict(encrypt_metadata=False, pdf_mac=False)),
    ("ps_two", "V 5, to Someone Else and then to Tiku Test Signer", [other_crt, crt], dict(pdf_mac=False)),
    ("ps_other", "V 5, to Someone Else alone", [other_crt], dict(pdf_mac=False)),
    ("ps_nocopy", "V 5, its text not to be copied out", [crt], dict(perms=noex, pdf_mac=False)),
]
for name, what, certs, kw in cases:
    try:
        data = sealed(certs, **kw)
        out.append("/* %s */" % what)
        arr(name, data)
    except Exception as e:
        print("could not make", name, ":", repr(e)[:300])
open(OUT.replace("cms.inc", "pubsec.inc"), "w").write("\n".join(out) + "\n")
out = main_out
print("pubsec ok")

# ---- signed documents: tests/fixtures/pdfsig.inc
from pyhanko.sign import signers, fields
from pyhanko.sign.fields import SigFieldSpec, SigSeedSubFilter, MDPPerm
from pyhanko.pdf_utils.incremental_writer import IncrementalPdfFileWriter
from pyhanko.pdf_utils import generic
from pyhanko_certvalidator.registry import SimpleCertificateStore

def akey(k):
    return ak.PrivateKeyInfo.load(k.private_bytes(serialization.Encoding.DER, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
rsa_signer = signers.SimpleSigner(signing_cert=acrt(crt), signing_key=akey(key), cert_registry=SimpleCertificateStore.from_certs([acrt(crt)]))
ec_signer = signers.SimpleSigner(signing_cert=acrt(ec_crt), signing_key=akey(eck), cert_registry=SimpleCertificateStore.from_certs([acrt(ec_crt)]))
tsa = DummyTimeStamper(tsa_cert=acrt(tsa_crt), tsa_key=akey(tsak),
                       fixed_dt=datetime.datetime(2026, 9, 28, 12, 34, 56, tzinfo=datetime.timezone.utc))

def signed(base, signer, name, box=None, ts=None, **meta):
    w = IncrementalPdfFileWriter(BytesIO(base))
    m = signers.PdfSignatureMetadata(field_name=name, **meta)
    spec = SigFieldSpec(name, on_page=0, box=box) if box else None
    return signers.sign_pdf(w, m, signer=signer, timestamper=ts, new_field_spec=spec,
                            bytes_reserved=6000 if ts else 3400).getvalue()

base = plain_pdf(b"Signed by its author")
sg = {}
sg["sg_rsa"] = signed(base, rsa_signer, "Author", box=(20, 20, 200, 60), md_algorithm="sha256",
                      reason="I wrote it", location="Singapore", contact_info="tiku@example.org",
                      subfilter=SigSeedSubFilter.ADOBE_PKCS7_DETACHED)
sg["sg_pades"] = signed(base, ec_signer, "Stamped", md_algorithm="sha384", ts=tsa,
                        subfilter=SigSeedSubFilter.PADES, reason="Approved")
cert = signed(base, rsa_signer, "Certifier", md_algorithm="sha256", certify=True,
              docmdp_permissions=MDPPerm.FILL_FORMS)
w = IncrementalPdfFileWriter(BytesIO(cert))
w.root["/PageMode"] = generic.NameObject("/UseNone")
w.update_root()
buf = BytesIO(); w.write(buf); sg["sg_certify"] = buf.getvalue()
sg["sg_two"] = signed(sg["sg_rsa"], ec_signer, "Reviewer", md_algorithm="sha256", reason="Checked")
w = IncrementalPdfFileWriter(BytesIO(sg["sg_rsa"]))
sg["sg_doctime"] = signers.PdfTimeStamper(tsa).timestamp_pdf(w, "sha256", bytes_reserved=3000).getvalue()
b = bytearray(sg["sg_rsa"]); i = b.find(b"Signed by its author"); b[i + 10] = ord("I")
changed = bytes(b)
b = bytearray(sg["sg_rsa"]); i = b.find(b"/Contents <") + 11
j = b.find(b"0000000000", i)          # the value ends where its padding starts
b[j - 5] = ord("0") if b[j - 5] != ord("0") else ord("1")
forged = bytes(b)
verdict = {}
for n, data in (("changed", changed), ("forged", forged)):
    p = os.path.join(W, n + ".pdf"); open(p, "wb").write(data)
    r = pdfsig(p)
    verdict[n] = "; ".join(l.split(": ", 1)[1].strip() for l in r.stdout.splitlines() if "Signature Validation:" in l)

def legacy(base, subfilter, extra, sign_fn):
    """An incremental update adding a signature field whose value is made
    by sign_fn(byte range data) -> the /Contents bytes."""
    x0 = base.rfind(b"startxref"); prev = int(base[x0 + 9:].split()[0])
    add = b"\n"; offs = {}
    start = len(base)
    def obj(n, body):
        nonlocal add
        offs[n] = start + len(add); add += b"%d 0 obj\n" % n + body + b"\nendobj\n"
    obj(7, b"<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /" + subfilter +
        b" /ByteRange [0 0000000000 0000000000 0000000000] /Contents <" + b"0" * 2400 +
        b"> /M (D:20260928120000+08'00') /Name (Tiku Test Signer) /Reason (The old way)" + extra + b" >>")
    obj(8, b"<< /FT /Sig /T (Legacy) /V 7 0 R /Type /Annot /Subtype /Widget /Rect [0 0 0 0] /P 3 0 R /F 132 >>")
    obj(1, b"<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [8 0 R] /SigFlags 3 >> >>")
    obj(3, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Contents 4 0 R "
           b"/Resources << /Font << /F1 5 0 R >> >> /Annots [8 0 R] >>")
    xref = start + len(add)
    add += b"xref\n0 1\n0000000000 65535 f \n1 1\n%010d 00000 n \n3 1\n%010d 00000 n \n7 2\n%010d 00000 n \n%010d 00000 n \n" % (offs[1], offs[3], offs[7], offs[8])
    add += b"trailer\n<< /Size 9 /Root 1 0 R /Info 6 0 R /Prev %d >>\nstartxref\n%d\n%%%%EOF\n" % (prev, xref)
    doc = bytearray(base + add)
    a = doc.find(b"/Contents <", start) + 10; bb = doc.find(b">", a) + 1
    br = b"0 %010d %010d %010d" % (a, bb, len(doc) - bb)
    k = doc.find(b"0 0000000000 0000000000 0000000000", start)
    doc[k:k + len(br)] = br
    data = bytes(doc[:a]) + bytes(doc[bb:])
    v = sign_fn(data).hex().encode()
    doc[a + 1:a + 1 + len(v)] = v
    return bytes(doc)

def pkcs7_sha1(data):
    d = os.path.join(W, "byterange.sha1")
    open(d, "wb").write(hashlib.sha1(data).digest())
    return ossl("cms", "-sign", "-binary", "-nodetach", "-md", "sha1", "-outform", "DER",
                "-signer", crt_pem, "-inkey", key_pem, "-in", d)
def x509_sha1(data):
    s = key.sign(data, padding.PKCS1v15(), hashes.SHA1())
    return bytes([0x04, 0x81, len(s)]) + s
sg["sg_sha1"] = legacy(base, b"adbe.pkcs7.sha1", b"", pkcs7_sha1)
certhex = crt.public_bytes(serialization.Encoding.DER).hex().encode()
sg["sg_x509"] = legacy(base, b"adbe.x509.rsa_sha1", b" /Cert <" + certhex + b">", x509_sha1)

what = {
    "sg_rsa": "pyHanko: adbe.pkcs7.detached, RSA, SHA-256; a visible field, reason, place and contact",
    "sg_pades": "pyHanko: ETSI.CAdES.detached, ECDSA P-256, SHA-384, a timestamp over it",
    "sg_certify": "pyHanko: a certifying signature (DocMDP, forms may be filled), then a revision after it",
    "sg_two": "pyHanko: sg_rsa, then a second signature by the EC key",
    "sg_doctime": "pyHanko: sg_rsa, then a document timestamp (ETSI.RFC3161)",
    "sg_sha1": "made here: adbe.pkcs7.sha1, its SignedData by LibreSSL cms -sign",
    "sg_x509": "made here: adbe.x509.rsa_sha1, its RSA signature by cryptography",
}
main_out = out
strip = lambda v: v.rstrip(".")
out = ["/* test_pdf_sign's signed documents; the key and certificates are",
       " * cms.inc's.  Each was shown to poppler's pdfsig (NSS) when made, and",
       " * its verdict follows the description.  sg_rsa with a letter of its",
       " * page changed, as the test changes it, pdfsig called %s; with a" % strip(verdict["changed"]),
       " * digit of its signature value changed, %s.  sg_two and" % strip(verdict["forged"]),
       " * sg_doctime are the revisions they add to sg_rsa.  Base64.",
       " * SPDX-License-Identifier: Apache-2.0 */"]
for n, data in sg.items():
    p = os.path.join(W, n + ".pdf"); open(p, "wb").write(data)
    r = pdfsig(p)
    verdicts = [strip(l.split(": ", 1)[1].strip()) for l in r.stdout.splitlines() if "Signature Validation:" in l]
    text = "%s.  pdfsig: %s" % (what[n], "; ".join(verdicts))
    words, line, lines = text.split(), "/*", []
    for word in words:
        if len(line) + 1 + len(word) > 76:
            lines.append(line); line = " *"
        line += " " + word
    lines.append(line + " */")
    out += lines
    if n in ("sg_two", "sg_doctime"):
        assert data.startswith(sg["sg_rsa"])
        data = data[len(sg["sg_rsa"]):]
    arr(n, data)
open(OUT.replace("cms.inc", "pdfsig.inc"), "w").write("\n".join(out) + "\n")
out = main_out
print("pdfsig ok")
