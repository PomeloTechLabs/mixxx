#!/usr/bin/env bash
# Validate that the signing material triple actually matches:
#   p12 (private key)  ==  .cer (app cert)  ==  .p7b (profile's dev-cert)
# Usage: verify-materials.sh <p12> <alias> <pwd> <cer> <p7b>
set -uo pipefail
TOOL=${HAP_SIGN_TOOL:-/apps/harmony/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar}
P12=$1; ALIAS=$2; PWD=$3; CER=$4; P7B=$5
FAIL=0

keytool -exportcert -alias "$ALIAS" -keystore "$P12" -storepass "$PWD" -rfc -file /tmp/v-p12.pem 2>/dev/null \
  || { echo "FAIL: cannot open p12 (alias '${ALIAS}' / password)"; exit 1; }

java -jar "$TOOL" verify-profile -inFile "$P7B" -outFile /tmp/v-prof.json >/dev/null 2>&1 \
  || { echo "FAIL: profile verification error"; exit 1; }
python3 - <<PY
import json
j=json.load(open("/tmp/v-prof.json"))
pem=j["content"]["bundle-info"]["development-certificate"]
if "\\n" in pem: pem=pem.replace("\\n","\n")
open("/tmp/v-prof.pem","w").write(pem if pem.endswith("\n") else pem+"\n")
print("profile bundle-name :", j["content"]["bundle-info"]["bundle-name"])
print("profile type        :", j["content"].get("type"))
print("profile device-ids  :", len(j["content"].get("debug-info",{}).get("device-ids",[])), "entries")
PY

pk() { openssl x509 -in "$1" -pubkey -noout 2>/dev/null | openssl sha256 | cut -d' ' -f2; }
A=$(pk /tmp/v-p12.pem); B=$(pk "$CER"); C=$(pk /tmp/v-prof.pem)
echo "p12   pubkey: $A"
echo "cer   pubkey: $B"
echo "prof  pubkey: $C"
[ "$A" = "$B" ] || { echo "FAIL: p12 != cer"; FAIL=1; }
[ "$B" = "$C" ] || { echo "FAIL: cer != profile cert"; FAIL=1; }
if ! diff -q /tmp/v-p12.pem "$CER" >/dev/null 2>&1; then
  echo "NOTE: p12 cert and cer file differ byte-wise (may still match by pubkey)"
fi
[ "$FAIL" -eq 0 ] && echo "OK: signing triple is consistent"
exit $FAIL
