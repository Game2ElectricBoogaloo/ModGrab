import re
import sys

import certifi

ROOTS = [
    "ISRG Root X1",
    "ISRG Root X2",
    "GTS Root R1",
    "GTS Root R3",
    "GTS Root R4",
    "GlobalSign Root R46",
    "GlobalSign Root E46",
    "SSL.com Root Certification Authority RSA",
    "SSL.com Root Certification Authority ECC",
    "SSL.com TLS RSA Root CA 2022",
    "SSL.com TLS ECC Root CA 2022",
    "Sectigo Public Server Authentication Root R46",
    "Sectigo Public Server Authentication Root E46",
    "USERTrust RSA Certification Authority",
    "USERTrust ECC Certification Authority",
    "DigiCert Global Root G2",
    "DigiCert Global Root G3",
    "Amazon Root CA 1",
    "Amazon Root CA 2",
    "Amazon Root CA 3",
    "Amazon Root CA 4",
]


def main(out_path):
    with open(certifi.where(), encoding="ascii") as bundle:
        text = bundle.read()
    blocks = dict(re.findall(r'# Label: "(.*?)"\n.*?(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)', text, re.S))
    missing = [name for name in ROOTS if name not in blocks]
    if missing:
        sys.exit("not in this certifi release: " + ", ".join(missing))
    with open(out_path, "w", encoding="ascii", newline="\n") as out:
        for name in ROOTS:
            out.write(f"# {name}\n{blocks[name]}\n")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "romfs/certs.pem")
