#!/bin/zsh

# DX10R macOS Release Build Script (zsh)
# - WebUI 本番ビルド → CMake (Xcode) で VST3/AU/CLAP/Standalone (+ AAX) Universal Binary
# - Hardened Runtime コード署名 → AAX PACE (iLok) 署名（任意）
# - 言語別ライセンス (en/ja) を持つ署名済み PKG → notarytool で公証 → stapler 添付
# - 互換用 ZIP も同時生成
#
# 注: このスクリプトは macOS でのみ動作する (Windows では未検証)。

set -e
set -u
set -o pipefail

color_cyan="\033[36m"; color_yellow="\033[33m"; color_green="\033[32m"
color_red="\033[31m"; color_gray="\033[90m"; color_reset="\033[0m"

echo_header()  { echo ""; echo -e "${color_cyan}============================================${color_reset}"; echo -e "${color_cyan}   $1${color_reset}"; echo -e "${color_cyan}============================================${color_reset}"; echo ""; }
echo_step()    { echo -e "${color_yellow}>> $1${color_reset}"; }
echo_success() { echo -e "${color_green}[OK] $1${color_reset}"; }
echo_warn()    { echo -e "${color_yellow}[!!] $1${color_reset}"; }
echo_error()   { echo -e "${color_red}[FAIL] $1${color_reset}" 1>&2; }

CONFIGURATION="Release"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --config|-c) CONFIGURATION="${2:-Release}"; shift 2 ;;
        --skip-webui) export SKIP_WEBUI="1"; shift 1 ;;
        *) echo_error "Unknown argument: $1"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="${SCRIPT_DIR}"
VERSION_FILE="${ROOT_DIR}/VERSION"

[[ -f "${VERSION_FILE}" ]] || { echo_error "VERSION file not found: ${VERSION_FILE}"; exit 1; }
VERSION="$(cat "${VERSION_FILE}" | tr -d '\r' | tr -d '\n')"
BUILD_DATE="$(date +%Y-%m-%d)"

# Load .env if present
ENV_FILE="${ROOT_DIR}/.env"
if [[ -f "${ENV_FILE}" ]]; then
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line## }"; line="${line%% }"
        [[ -z "$line" || "$line" == \#* ]] && continue
        key="${line%%=*}"; value="${line#*=}"
        value="${value#\"}"; value="${value%\"}"; value="${value#\'}"; value="${value%\'}"
        if [[ -z "${(P)key:-}" ]]; then export "$key=$value"; fi
    done < "${ENV_FILE}"
fi

echo_header "DX10R ${VERSION} Build Script (macOS zsh)"

WEBUI_DIR="${ROOT_DIR}/webui"
BUILD_DIR="${ROOT_DIR}/build-dist"
OUTPUT_DIR="${ROOT_DIR}/releases/${VERSION}/macOS"
AAX_SDK_PATH="${ROOT_DIR}/iPlug2/Dependencies/IPlug/AAX_SDK"

echo_step "Checking AAX SDK..."
if [[ -f "${AAX_SDK_PATH}/Interfaces/AAX.h" ]]; then
    echo_success "AAX SDK found - AAX will be built"
    BUILD_AAX=1
    AAX_LIB_CMAKE="${AAX_SDK_PATH}/Libs/AAXLibrary/CMakeLists.txt"
    if [[ -f "${AAX_LIB_CMAKE}" ]] && ! grep -qF 'BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Interfaces' "${AAX_LIB_CMAKE}"; then
        echo_step "Patching AAX SDK CMakeLists.txt for CMake 4.x compatibility..."
        /usr/bin/sed -i '' \
            -e 's|^    \${CMAKE_CURRENT_SOURCE_DIR}/\.\./\.\./Interfaces$|    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Interfaces>|' \
            -e 's|^    \${CMAKE_CURRENT_SOURCE_DIR}/\.\./\.\./Interfaces/ACF$|    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Interfaces/ACF>|' \
            "${AAX_LIB_CMAKE}"
        echo_success "AAX SDK patched"
    fi
else
    echo_warn "AAX SDK not found at ${AAX_SDK_PATH} - AAX will be skipped"
    BUILD_AAX=0
fi

mkdir -p "${OUTPUT_DIR}"

# ----------------------------------------------------------------------------
# Step 1: WebUI build
# ----------------------------------------------------------------------------
echo_header "Step 1: Building WebUI for production"
if [[ "${SKIP_WEBUI:-0}" != "1" ]]; then
    [[ -d "${WEBUI_DIR}" ]] || { echo_error "WebUI directory not found: ${WEBUI_DIR}"; exit 1; }
    WEB_OUT_DIR="${ROOT_DIR}/plugin/resources/web"
    [[ -d "${WEB_OUT_DIR}" ]] && rm -rf "${WEB_OUT_DIR}"
    pushd "${WEBUI_DIR}" >/dev/null
    echo_step "Installing npm dependencies..."; npm install --no-audit --no-fund
    echo_step "Building WebUI..."; npm run build
    popd >/dev/null
    [[ -f "${WEB_OUT_DIR}/index.html" ]] || { echo_error "WebUI build output not found at ${WEB_OUT_DIR}"; exit 1; }
    echo_success "WebUI build completed"
else
    echo_step "Skipping WebUI build (SKIP_WEBUI=1)"
fi

# ----------------------------------------------------------------------------
# Step 2: Native plugin build (Universal Binary)
# ----------------------------------------------------------------------------
echo_header "Step 2: Building Plugins (VST3/AU/CLAP/Standalone$([[ ${BUILD_AAX} -eq 1 ]] && echo '/AAX'))"

echo_step "CMake configuration (${CONFIGURATION}, Universal Binary)..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -G Xcode \
    -DCMAKE_BUILD_TYPE="${CONFIGURATION}" \
    -DIPLUG2_UNIVERSAL=ON \
    -DIPLUG_DEPLOY_PLUGINS=OFF

TARGETS=(DX10R-vst3 DX10R-au DX10R-clap DX10R-app)
if [[ ${BUILD_AAX} -eq 1 ]]; then TARGETS+=(DX10R-aax); fi

echo_step "Executing build (${TARGETS[@]})..."
cmake --build "${BUILD_DIR}" --config "${CONFIGURATION}" --target ${TARGETS[@]}
echo_success "Plugin build completed"

# Xcode generator appends $(CONFIGURATION) to the output dir.
ARTIFACTS_DIR="${BUILD_DIR}/out/${CONFIGURATION}"
SRC_VST3="${ARTIFACTS_DIR}/DX10R.vst3"
SRC_AU="${ARTIFACTS_DIR}/DX10R.component"
SRC_CLAP="${ARTIFACTS_DIR}/DX10R.clap"
SRC_APP="${ARTIFACTS_DIR}/DX10R.app"

[[ -d "${SRC_VST3}" ]] || { echo_error "VST3 not found: ${SRC_VST3}"; exit 1; }
[[ -d "${SRC_AU}" ]]   || { echo_error "AU not found: ${SRC_AU}";     exit 1; }
[[ -d "${SRC_APP}" ]]  || { echo_error "Standalone not found: ${SRC_APP}"; exit 1; }
[[ -d "${SRC_CLAP}" ]] || echo_warn "CLAP not found: ${SRC_CLAP} (skipping CLAP)"

if [[ ${BUILD_AAX} -eq 1 ]]; then
    SRC_AAX="${ARTIFACTS_DIR}/DX10R.aaxplugin"
    [[ -d "${SRC_AAX}" ]] || { echo_error "AAX not found: ${SRC_AAX}"; exit 1; }
fi

echo_step "Collecting artifacts..."
DEST_VST3="${OUTPUT_DIR}/DX10R.vst3"
DEST_AU="${OUTPUT_DIR}/DX10R.component"
DEST_CLAP="${OUTPUT_DIR}/DX10R.clap"
DEST_APP="${OUTPUT_DIR}/DX10R.app"
rm -rf "${DEST_VST3}" "${DEST_AU}" "${DEST_CLAP}" "${DEST_APP}"
cp -R "${SRC_VST3}" "${DEST_VST3}"
cp -R "${SRC_AU}"   "${DEST_AU}"
cp -R "${SRC_APP}"  "${DEST_APP}"
[[ -d "${SRC_CLAP}" ]] && cp -R "${SRC_CLAP}" "${DEST_CLAP}"
HAVE_CLAP=$([[ -d "${DEST_CLAP}" ]] && echo 1 || echo 0)

if [[ ${BUILD_AAX} -eq 1 ]]; then
    DEST_AAX="${OUTPUT_DIR}/DX10R.aaxplugin"
    rm -rf "${DEST_AAX}"; cp -R "${SRC_AAX}" "${DEST_AAX}"
fi

# ----------------------------------------------------------------------------
# Step 3: Codesign (Hardened Runtime)
# ----------------------------------------------------------------------------
echo_header "Step 3: Code Signing (Hardened Runtime)"
if [[ -z "${CODESIGN_IDENTITY:-}" ]]; then
    if [[ -n "${CODESIGN_TEAM_ID:-}" ]]; then
        CODESIGN_IDENTITY=$(security find-identity -v -p codesigning 2>/dev/null | awk -v team="${CODESIGN_TEAM_ID}" -F '"' '/Developer ID Application:/ && $0 ~ team {print $2; exit}') || true
    fi
    if [[ -z "${CODESIGN_IDENTITY:-}" ]]; then
        CODESIGN_IDENTITY=$(security find-identity -v -p codesigning 2>/dev/null | awk -F '"' '/Developer ID Application:/ {print $2; exit}') || true
    fi
    [[ -n "${CODESIGN_IDENTITY:-}" ]] || { echo_error "CODESIGN_IDENTITY not found. Install a Developer ID Application certificate and retry."; exit 1; }
    echo_success "Auto-selected signing ID: ${CODESIGN_IDENTITY}"
fi

sign_bundle() {
    local bundle_path="$1"; local entitlements_args=()
    if [[ -n "${ENTITLEMENTS_PATH:-}" && -f "${ENTITLEMENTS_PATH}" ]]; then entitlements_args=(--entitlements "${ENTITLEMENTS_PATH}"); fi
    codesign --force --timestamp --options runtime "${entitlements_args[@]}" --sign "${CODESIGN_IDENTITY}" "${bundle_path}"
    codesign --verify --deep --strict --verbose=2 "${bundle_path}"
}

echo_step "Signing VST3...";       sign_bundle "${DEST_VST3}"; echo_success "VST3 OK"
echo_step "Signing AU...";         sign_bundle "${DEST_AU}";   echo_success "AU OK"
echo_step "Signing Standalone..."; sign_bundle "${DEST_APP}";  echo_success "Standalone OK"
[[ ${HAVE_CLAP} -eq 1 ]] && { echo_step "Signing CLAP..."; sign_bundle "${DEST_CLAP}"; echo_success "CLAP OK"; }
if [[ ${BUILD_AAX} -eq 1 ]]; then
    echo_step "Signing AAX (developer signature, before PACE)..."; sign_bundle "${DEST_AAX}"; echo_success "AAX dev OK"
fi

# ----------------------------------------------------------------------------
# Step 3.5: AAX PACE / iLok signing (optional)
# ----------------------------------------------------------------------------
AAX_PACE_STATUS="not_attempted"
if [[ ${BUILD_AAX} -eq 1 ]]; then
    echo_header "Step 3.5: AAX PACE (iLok) signing"
    PACE_WCGUID_EFFECTIVE="${WRAP_GUID:-${PACE_ORGANIZATION:-}}"
    WRAPTOOL_CANDIDATES=()
    [[ -n "${WRAPTOOL_PATH:-}" ]] && WRAPTOOL_CANDIDATES+=("${WRAPTOOL_PATH}")
    WRAPTOOL_CANDIDATES+=(
        "/Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool"
        "/Applications/PACE Anti-Piracy/Eden/Fusion/Versions/5/bin/wraptool"
        "/usr/local/bin/wraptool")
    FOUND_WRAPTOOL=""
    for p in "${WRAPTOOL_CANDIDATES[@]}"; do [[ -x "$p" ]] && { FOUND_WRAPTOOL="$p"; break; }; done

    MISSING=()
    [[ -z "${PACE_USERNAME:-}" ]] && MISSING+=("PACE_USERNAME")
    [[ -z "${PACE_PASSWORD:-}" ]] && MISSING+=("PACE_PASSWORD")
    [[ -z "${PACE_WCGUID_EFFECTIVE}" ]] && MISSING+=("WRAP_GUID")

    if [[ -z "${FOUND_WRAPTOOL}" ]]; then
        echo_warn "wraptool not found - skipping PACE signing"; AAX_PACE_STATUS="wraptool_missing"
    elif (( ${#MISSING[@]} > 0 )); then
        echo_warn "Missing PACE creds: ${MISSING[*]} - skipping PACE signing"; AAX_PACE_STATUS="credentials_missing"
    else
        echo_step "Signing AAX with PACE wraptool..."
        if "${FOUND_WRAPTOOL}" sign --verbose \
                --account "${PACE_USERNAME}" --password "${PACE_PASSWORD}" \
                --wcguid "${PACE_WCGUID_EFFECTIVE}" --signid "${CODESIGN_IDENTITY}" \
                --dsigharden --dsig1-compat on --in "${DEST_AAX}" --out "${DEST_AAX}"; then
            echo_success "AAX PACE signed"; AAX_PACE_STATUS="signed"
        else
            echo_warn "AAX PACE signing failed - continuing developer-signed"; AAX_PACE_STATUS="signing_failed"
        fi
    fi
fi

# ----------------------------------------------------------------------------
# Step 4: PKG composition with localized licenses (en / ja)
# ----------------------------------------------------------------------------
echo_header "Step 4: Building component PKGs and localized product PKG"
PKG_WORK_DIR="${OUTPUT_DIR}/pkgwork"
mkdir -p "${PKG_WORK_DIR}"
PKG_ID_BASE="${PKG_ID_BASE:-com.junmurakami.dx10r}"

_hexof() { printf '%s' "$1" | xxd -p; }

# AU bundle から Logic tagset 名を算出し、既定カテゴリを seed する postinstall を生成。
emit_au_logic_tagset_postinstall() {
    local au_bundle="$1" category="$2" scripts_dir="$3"
    local plist="${au_bundle}/Contents/Info.plist"
    local au_type au_sub au_man tagset_name
    au_type=$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:type"         "${plist}")
    au_sub=$(/usr/libexec/PlistBuddy  -c "Print :AudioComponents:0:subtype"      "${plist}")
    au_man=$(/usr/libexec/PlistBuddy  -c "Print :AudioComponents:0:manufacturer" "${plist}")
    tagset_name="$(_hexof "${au_type}")-$(_hexof "${au_sub}")-$(_hexof "${au_man}").tagset"
    echo_step "AU Logic category seed: ${category} (tagset=${tagset_name})"
    rm -rf "${scripts_dir}" && mkdir -p "${scripts_dir}"
    cat > "${scripts_dir}/postinstall" <<'POSTINSTALL'
#!/bin/sh
set -u
TAGSET_NAME="@@TAGSET_NAME@@"
CATEGORY="@@CATEGORY@@"
CONSOLE_USER="$(/usr/bin/stat -f%Su /dev/console 2>/dev/null)"
[ -n "$CONSOLE_USER" ] || exit 0
[ "$CONSOLE_USER" != "root" ] || exit 0
USER_HOME="$(/usr/bin/dscl . -read /Users/"$CONSOLE_USER" NFSHomeDirectory 2>/dev/null | /usr/bin/awk '{print $2}')"
[ -n "$USER_HOME" ] || exit 0
TAGDIR="$USER_HOME/Music/Audio Music Apps/Databases/Tags"
[ -d "$TAGDIR" ] || exit 0
[ -w "$TAGDIR" ] || exit 0
TAGFILE="$TAGDIR/$TAGSET_NAME"
if [ -e "$TAGFILE" ] && /usr/libexec/PlistBuddy -c "Print :tags" "$TAGFILE" 2>/dev/null | grep -q ' = '; then exit 0; fi
TMP="$TAGDIR/.au_logic_tagset.$$"
cat > "$TMP" 2>/dev/null <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>tags</key><dict><key>$CATEGORY</key><string>tag</string></dict></dict></plist>
PLIST
[ -s "$TMP" ] || { rm -f "$TMP" 2>/dev/null; exit 0; }
if /usr/bin/plutil -lint "$TMP" >/dev/null 2>&1; then
    /bin/mv -f "$TMP" "$TAGFILE" 2>/dev/null || { rm -f "$TMP" 2>/dev/null; exit 0; }
    /usr/sbin/chown "$CONSOLE_USER" "$TAGFILE" 2>/dev/null || true
else
    rm -f "$TMP" 2>/dev/null
fi
exit 0
POSTINSTALL
    /usr/bin/sed -i '' -e "s/@@TAGSET_NAME@@/${tagset_name}/" -e "s/@@CATEGORY@@/${category}/" "${scripts_dir}/postinstall"
    chmod +x "${scripts_dir}/postinstall"
}

build_component_pkg() {
    local kind="$1" src="$2" dst_path="$3" logic_category="${4:-}"
    local pkgroot="${PKG_WORK_DIR}/root_${kind}"
    rm -rf "${pkgroot}"; mkdir -p "${pkgroot}${dst_path}"
    cp -R "${src}" "${pkgroot}${dst_path}/"
    local -a scripts_args=()
    if [[ -n "${logic_category}" ]]; then
        local scripts_dir="${PKG_WORK_DIR}/scripts_${kind}"
        emit_au_logic_tagset_postinstall "${src}" "${logic_category}" "${scripts_dir}"
        scripts_args=(--scripts "${scripts_dir}")
    fi
    pkgbuild --root "${pkgroot}" --identifier "${PKG_ID_BASE}.${kind}" --version "${VERSION}" \
        --install-location "/" --ownership recommended "${scripts_args[@]}" \
        "${PKG_WORK_DIR}/DX10R_${kind}.pkg"
}

build_app_pkg() {
    local kind="$1" src="$2"
    local appname="$(basename "${src}")"
    local pkgroot="${PKG_WORK_DIR}/root_${kind}"
    rm -rf "${pkgroot}"; mkdir -p "${pkgroot}"; cp -R "${src}" "${pkgroot}/"
    local cplist="${PKG_WORK_DIR}/${kind}-component.plist"
    cat > "${cplist}" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><array><dict>
  <key>BundleHasStrictIdentifier</key><true/>
  <key>BundleIsRelocatable</key><false/>
  <key>BundleIsVersionChecked</key><false/>
  <key>BundleOverwriteAction</key><string>upgrade</string>
  <key>RootRelativeBundlePath</key><string>${appname}</string>
</dict></array></plist>
PLIST
    pkgbuild --root "${pkgroot}" --identifier "${PKG_ID_BASE}.${kind}" --version "${VERSION}" \
        --install-location "/Applications" --component-plist "${cplist}" --ownership recommended \
        "${PKG_WORK_DIR}/DX10R_${kind}.pkg"
}

SYNTH_LOGIC_CATEGORY="${SYNTH_LOGIC_CATEGORY:-Synthesizer}"

build_component_pkg "vst3" "${DEST_VST3}" "/Library/Audio/Plug-Ins/VST3"
build_component_pkg "au"   "${DEST_AU}"   "/Library/Audio/Plug-Ins/Components" "${SYNTH_LOGIC_CATEGORY}"
[[ ${HAVE_CLAP} -eq 1 ]] && build_component_pkg "clap" "${DEST_CLAP}" "/Library/Audio/Plug-Ins/CLAP"
build_app_pkg "app" "${DEST_APP}"
if [[ ${BUILD_AAX} -eq 1 ]]; then
    build_component_pkg "aax" "${DEST_AAX}" "/Library/Application Support/Avid/Audio/Plug-Ins"
fi

RESOURCES_DIR="${PKG_WORK_DIR}/resources"
mkdir -p "${RESOURCES_DIR}/en.lproj" "${RESOURCES_DIR}/ja.lproj"
[[ -f "${ROOT_DIR}/LICENSE" ]]       && cp "${ROOT_DIR}/LICENSE"       "${RESOURCES_DIR}/en.lproj/License.txt"
[[ -f "${ROOT_DIR}/LICENSE.ja.md" ]] && cp "${ROOT_DIR}/LICENSE.ja.md" "${RESOURCES_DIR}/ja.lproj/License.txt"

DIST_XML="${PKG_WORK_DIR}/Distribution.xml"
{
    echo "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    echo "<installer-gui-script minSpecVersion=\"1\">"
    echo "  <title>DX10R ${VERSION}</title>"
    echo "  <options customize=\"always\" allow-external-scripts=\"no\"/>"
    echo "  <domains enable_currentUserHome=\"true\" enable_localSystem=\"true\"/>"
    [[ -f "${RESOURCES_DIR}/en.lproj/License.txt" ]] && echo "  <license file=\"License.txt\"/>"
    echo "  <choices-outline>"
    echo "    <line choice=\"choice_au\"/>"
    echo "    <line choice=\"choice_vst3\"/>"
    [[ ${HAVE_CLAP} -eq 1 ]] && echo "    <line choice=\"choice_clap\"/>"
    [[ ${BUILD_AAX} -eq 1 ]] && echo "    <line choice=\"choice_aax\"/>"
    echo "    <line choice=\"choice_app\"/>"
    echo "  </choices-outline>"
    echo "  <choice id=\"choice_au\" title=\"DX10R (AU)\" enabled=\"true\" selected=\"true\"><pkg-ref id=\"${PKG_ID_BASE}.au\"/></choice>"
    echo "  <choice id=\"choice_vst3\" title=\"DX10R (VST3)\" enabled=\"true\" selected=\"true\"><pkg-ref id=\"${PKG_ID_BASE}.vst3\"/></choice>"
    [[ ${HAVE_CLAP} -eq 1 ]] && echo "  <choice id=\"choice_clap\" title=\"DX10R (CLAP)\" enabled=\"true\" selected=\"true\"><pkg-ref id=\"${PKG_ID_BASE}.clap\"/></choice>"
    [[ ${BUILD_AAX} -eq 1 ]] && echo "  <choice id=\"choice_aax\" title=\"DX10R (AAX, Pro Tools)\" enabled=\"true\" selected=\"true\"><pkg-ref id=\"${PKG_ID_BASE}.aax\"/></choice>"
    echo "  <choice id=\"choice_app\" title=\"DX10R (Standalone)\" enabled=\"true\" selected=\"true\"><pkg-ref id=\"${PKG_ID_BASE}.app\"/></choice>"
    echo "  <pkg-ref id=\"${PKG_ID_BASE}.vst3\">DX10R_vst3.pkg</pkg-ref>"
    echo "  <pkg-ref id=\"${PKG_ID_BASE}.au\">DX10R_au.pkg</pkg-ref>"
    [[ ${HAVE_CLAP} -eq 1 ]] && echo "  <pkg-ref id=\"${PKG_ID_BASE}.clap\">DX10R_clap.pkg</pkg-ref>"
    echo "  <pkg-ref id=\"${PKG_ID_BASE}.app\">DX10R_app.pkg</pkg-ref>"
    [[ ${BUILD_AAX} -eq 1 ]] && echo "  <pkg-ref id=\"${PKG_ID_BASE}.aax\">DX10R_aax.pkg</pkg-ref>"
    echo "</installer-gui-script>"
} > "${DIST_XML}"

if [[ -z "${INSTALLER_IDENTITY:-}" ]]; then
    INSTALLER_IDENTITY=$(security find-identity -v 2>/dev/null | awk -F '"' '/Developer ID Installer:/ {print $2; exit}') || true
    [[ -n "${INSTALLER_IDENTITY:-}" ]] || { echo_error "Developer ID Installer certificate not found"; exit 1; }
fi

PRODUCT_PKG_PATH="${OUTPUT_DIR}/../DX10R_${VERSION}_macOS.pkg"
echo_step "productbuild + sign..."
productbuild --distribution "${DIST_XML}" --package-path "${PKG_WORK_DIR}" \
    --resources "${RESOURCES_DIR}" --sign "${INSTALLER_IDENTITY}" "${PRODUCT_PKG_PATH}"
echo_success "Signed product PKG: ${PRODUCT_PKG_PATH}"

# ----------------------------------------------------------------------------
# Step 5: Notarize + staple
# ----------------------------------------------------------------------------
echo_header "Step 5: Notarization and stapling"
API_KEY_ID_EFFECTIVE="${APPLE_API_KEY_ID:-${APPLE_API_KEY:-}}"
if [[ -n "${APPLE_API_KEY_PATH:-}" && -n "${API_KEY_ID_EFFECTIVE}" && -n "${APPLE_API_ISSUER:-}" ]]; then
    xcrun notarytool submit "${PRODUCT_PKG_PATH}" --key "${APPLE_API_KEY_PATH}" --key-id "${API_KEY_ID_EFFECTIVE}" --issuer "${APPLE_API_ISSUER}" --wait
elif [[ -n "${NOTARYTOOL_PROFILE:-}" ]]; then
    xcrun notarytool submit "${PRODUCT_PKG_PATH}" --keychain-profile "${NOTARYTOOL_PROFILE}" --wait
elif [[ -n "${APPLE_ID:-}" && -n "${APP_PASSWORD:-}" && -n "${TEAM_ID:-}" ]]; then
    xcrun notarytool submit "${PRODUCT_PKG_PATH}" --apple-id "${APPLE_ID}" --password "${APP_PASSWORD}" --team-id "${TEAM_ID}" --wait
else
    echo_warn "Notarization credentials not set - skipping notarization (PKG is signed but not notarized)"
fi
xcrun stapler staple "${PRODUCT_PKG_PATH}" 2>/dev/null && echo_success "Stapling completed" || echo_warn "Stapling skipped (not notarized)"

# ----------------------------------------------------------------------------
# Step 6: ZIP for compatibility / direct download
# ----------------------------------------------------------------------------
echo_header "Step 6: Creating compatibility ZIP"
cat > "${OUTPUT_DIR}/ReadMe.txt" <<EOF
DX10R ${VERSION} - macOS Installation Guide
====================================================
1. Close your DAW before proceeding.
2. VST3:       copy DX10R.vst3 to ~/Library/Audio/Plug-Ins/VST3/ (or /Library/...)
3. AU:         copy DX10R.component to ~/Library/Audio/Plug-Ins/Components/
4. CLAP:       copy DX10R.clap to ~/Library/Audio/Plug-Ins/CLAP/
5. Standalone: copy DX10R.app to /Applications/.
EOF
if [[ ${BUILD_AAX} -eq 1 ]]; then
cat >> "${OUTPUT_DIR}/ReadMe.txt" <<EOF
6. AAX (Pro Tools): copy DX10R.aaxplugin to /Library/Application Support/Avid/Audio/Plug-Ins/
   Note: this build is ${AAX_PACE_STATUS}. AAX must be PACE-signed for production Pro Tools.
EOF
fi

ZIP_FORMATS="VST3_AU"
[[ ${HAVE_CLAP} -eq 1 ]] && ZIP_FORMATS="${ZIP_FORMATS}_CLAP"
[[ ${BUILD_AAX} -eq 1 ]] && ZIP_FORMATS="${ZIP_FORMATS}_AAX"
ZIP_FORMATS="${ZIP_FORMATS}_Standalone"
cat > "${OUTPUT_DIR}/version.json" <<EOF
{ "name": "DX10R", "version": "${VERSION}", "build_date": "${BUILD_DATE}", "platform": "macOS",
  "architecture": "universal", "webui": "embedded", "build_type": "${CONFIGURATION}",
  "aax_signing": "$([[ ${BUILD_AAX} -eq 1 ]] && echo ${AAX_PACE_STATUS} || echo N/A)" }
EOF
[[ -f "${ROOT_DIR}/LICENSE" ]] && cp "${ROOT_DIR}/LICENSE" "${OUTPUT_DIR}/LICENSE.txt"

ZIP_NAME="DX10R_${VERSION}_macOS_${ZIP_FORMATS}.zip"
ZIP_PATH="${OUTPUT_DIR}/../${ZIP_NAME}"
[[ -f "${ZIP_PATH}" ]] && rm -f "${ZIP_PATH}"
echo_step "Creating ZIP: ${ZIP_NAME}..."
(
    cd "${OUTPUT_DIR}"
    ZIP_ITEMS=("$(basename "${DEST_VST3}")" "$(basename "${DEST_AU}")" "$(basename "${DEST_APP}")")
    [[ ${HAVE_CLAP} -eq 1 ]] && ZIP_ITEMS+=("$(basename "${DEST_CLAP}")")
    [[ ${BUILD_AAX} -eq 1 ]] && ZIP_ITEMS+=("$(basename "${DEST_AAX}")")
    ZIP_ITEMS+=(ReadMe.txt version.json)
    [[ -f LICENSE.txt ]] && ZIP_ITEMS+=(LICENSE.txt)
    /usr/bin/zip -r -y "${ZIP_PATH}" "${ZIP_ITEMS[@]}" >/dev/null
)
echo_success "ZIP: ${ZIP_PATH}"

echo_header "Build completed successfully!"
echo "PKG: ${PRODUCT_PKG_PATH}"
echo "ZIP: ${ZIP_PATH}"
exit 0
