cmake_minimum_required(VERSION 3.22)

project(miryu-recovery VERSION 45.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Widgets)
find_package(KF6 6.0 REQUIRED COMPONENTS CoreAddons DBusAddons I18n)

set(MIRYU_LIBEXECDIR "${CMAKE_INSTALL_PREFIX}/libexec" CACHE PATH
    "Directory for the Miryu Recovery executables")
set(MIRYU_HELPER_PATH "${MIRYU_LIBEXECDIR}/miryu-recovery-helper")

add_compile_definitions(TRANSLATION_DOMAIN="miryu-recovery")

set(snapshot_sources
    src/snapshotinfo.cpp
    src/snapshotinfo.h
)

add_executable(miryu-recovery
    src/main.cpp
    src/recoverynotifier.cpp
    src/recoverynotifier.h
    ${snapshot_sources}
)
target_link_libraries(miryu-recovery PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
    KF6::CoreAddons
    KF6::DBusAddons
    KF6::I18n
)

add_executable(miryu-recovery-helper
    src/helper.cpp
    ${snapshot_sources}
)
target_link_libraries(miryu-recovery-helper PRIVATE Qt6::Core Qt6::DBus)

install(TARGETS miryu-recovery miryu-recovery-helper
    DESTINATION ${MIRYU_LIBEXECDIR}
)

configure_file(data/miryu-recovery.service.in miryu-recovery.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/miryu-recovery.service
    DESTINATION lib/systemd/user
)
configure_file(data/org.miryugaming.recovery.helper.service.in org.miryugaming.recovery.helper.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.miryugaming.recovery.helper.service
    DESTINATION ${KDE_INSTALL_DBUSSYSTEMSERVICEDIR}
)
install(FILES data/org.miryugaming.recovery.helper.conf
    DESTINATION ${KDE_INSTALL_DBUSDIR}/system.d
)
install(FILES data/org.miryugaming.recovery.policy
    DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/actions
)
install(FILES data/miryu-recovery.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)
install(FILES data/miryu-recovery-autostart.desktop
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart
    RENAME miryu-recovery.desktop
)
install(FILES data/miryu-recovery.notifyrc
    DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
)

ki18n_install(po)
