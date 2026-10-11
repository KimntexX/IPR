# Public payloads never copy local profiles or audited calibration files.
if(IPR_DEPLOY_MODE STREQUAL "standalone")
    if(Qt6_VERSION VERSION_LESS 6.8)
        message(FATAL_ERROR "Standalone deployment requires Qt >= 6.8; system builds support Qt >= 6.4")
    endif()
    if(WIN32)
        qt_generate_deploy_qml_app_script(TARGET appIPR OUTPUT_SCRIPT ipr_deploy
            DEPLOY_TOOL_OPTIONS --include-plugins qoffscreen)
    elseif(APPLE)
        qt_generate_deploy_qml_app_script(TARGET appIPR OUTPUT_SCRIPT ipr_deploy
            DEPLOY_TOOL_OPTIONS -always-overwrite)
    else()
        qt_generate_deploy_script(TARGET appIPR OUTPUT_SCRIPT ipr_deploy CONTENT "
            qt_deploy_qml_imports(TARGET appIPR PLUGINS_FOUND ipr_qml_plugins)
            qt_deploy_runtime_dependencies(
                EXECUTABLE \"\${QT_DEPLOY_BIN_DIR}/appIPR\"
                ADDITIONAL_EXECUTABLES \"\${QT_DEPLOY_BIN_DIR}/iprGenerateTests\"
                ADDITIONAL_MODULES \${ipr_qml_plugins}
                GENERATE_QT_CONF
                PRE_EXCLUDE_REGEXES \"^ld-linux\" \"^lib(c|m|dl|rt|pthread|resolv)\\.so\"
                POST_EXCLUDE_REGEXES \"/ld-linux[^/]*$\" \"/lib(c|m|dl|rt|pthread|resolv)\\.so[^/]*$\"
            )")
    endif()
    install(SCRIPT "${ipr_deploy}")
endif()

set(CPACK_PACKAGE_NAME "ipr")
set(CPACK_PACKAGE_VENDOR "IPR contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "IPR photographic RAW editor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "https://github.com/KimntexX/IPR/issues")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "IPR contributors")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qml6-module-qtcore, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs, qml6-module-qtqml-workerscript, qml6-module-qtquick-window, qml6-module-qtquick-templates, qt6-image-formats-plugins")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtimageformats")
string(REPLACE "-" "~" CPACK_DEBIAN_PACKAGE_VERSION "${IPR_RELEASE_LABEL}")
if(IPR_RELEASE_LABEL MATCHES "alpha")
    set(CPACK_RPM_PACKAGE_RELEASE "0.alpha")
elseif(IPR_RELEASE_LABEL MATCHES "beta")
    set(CPACK_RPM_PACKAGE_RELEASE "0.beta")
else()
    set(CPACK_RPM_PACKAGE_RELEASE "1")
endif()
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
include(CPack)
