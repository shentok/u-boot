#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Generates a throwaway HABv4 test PKI in the layout of NXP's
# hab4_pki_tree.sh: a CA signing four Super Root Keys (SRK), where SRK1
# signs a CSF and an image key. Certificates go to <dir>/crts, the private
# keys (password "test", see keys/key_pass.txt) to <dir>/keys, where CST
# looks them up. Also creates the SRK table and the SRK hash fuse values.
#
# usage: hab-pki.sh <dir>

set -e

dir=$1
mkdir -p "$dir/crts" "$dir/keys"
cd "$dir"

pass=test
printf '%s\n%s\n' $pass $pass > keys/key_pass.txt
suffix=sha256_2048_65537_v3

cat > v3_ca.cnf <<EOF
[req]
distinguished_name = dn
[dn]
[v3_ca]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer:always
basicConstraints = critical,CA:true
keyUsage = keyCertSign
EOF

cat > v3_usr.cnf <<EOF
[v3_usr]
basicConstraints = CA:FALSE
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
EOF

genkey() {
	openssl genpkey -quiet -algorithm RSA -pkeyopt rsa_keygen_bits:2048 \
		-aes-256-cbc -pass pass:$pass -out keys/$1_key.pem
}

# <name> <issuer> <extensions> <serial>
gencert() {
	genkey "$1"
	openssl req -new -key keys/$1_key.pem -passin pass:$pass \
		-subj "/CN=$1/" -out $1.csr
	openssl x509 -req -in $1.csr -sha256 -days 3650 -set_serial $4 \
		-CA crts/$2_crt.pem -CAkey keys/$2_key.pem -passin pass:$pass \
		-extfile $3.cnf -extensions $3 -out crts/$1_crt.pem
	rm $1.csr
}

ca=CA1_${suffix}_ca
genkey $ca
openssl req -new -x509 -config v3_ca.cnf -extensions v3_ca -sha256 \
	-days 3650 -key keys/${ca}_key.pem -passin pass:$pass \
	-subj "/CN=$ca/" -out crts/${ca}_crt.pem

srks=
for i in 1 2 3 4; do
	gencert SRK${i}_${suffix}_ca $ca v3_ca $i
	srks=$srks${srks:+,}crts/SRK${i}_${suffix}_ca_crt.pem
done
gencert CSF1_1_${suffix}_usr SRK1_${suffix}_ca v3_usr 11
gencert IMG1_1_${suffix}_usr SRK1_${suffix}_ca v3_usr 12

srktool -h 4 -t crts/SRK_1_2_3_4_table.bin -e crts/SRK_1_2_3_4_fuse.bin \
	-d sha256 -c "$srks" -f 1
rm v3_ca.cnf v3_usr.cnf
