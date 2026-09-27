#!/usr/bin/env bash
# Sign the unsigned HAP with hap-sign-tool.
# Env: KEY_ALIAS (default ad), KEY_PWD, CERT_FILE, PROFILE_FILE, KEYSTORE_FILE,
#      IN_HAP, OUT_HAP
set -euo pipefail
TOOL=${HAP_SIGN_TOOL:-/apps/harmony/sdk/default/openharmony/toolchains/lib/hap-sign-tool.jar}
: "${KEY_PWD:?KEY_PWD required}"
: "${KEYSTORE_FILE:?KEYSTORE_FILE required}"
: "${CERT_FILE:?CERT_FILE required}"
: "${PROFILE_FILE:?PROFILE_FILE required}"
IN_HAP=${IN_HAP:?IN_HAP required}
OUT_HAP=${OUT_HAP:-${IN_HAP%-unsigned.hap}-signed.hap}
KEY_ALIAS=${KEY_ALIAS:-ad}

java -jar "${TOOL}" sign-app \
  -mode localSign \
  -keyAlias "${KEY_ALIAS}" \
  -keyPwd "${KEY_PWD}" \
  -signAlg "SHA256withECDSA" \
  -appCertFile "${CERT_FILE}" \
  -profileFile "${PROFILE_FILE}" \
  -inFile "${IN_HAP}" \
  -keystoreFile "${KEYSTORE_FILE}" \
  -keystorePwd "${KEY_PWD}" \
  -outFile "${OUT_HAP}"
echo "signed: ${OUT_HAP}"
