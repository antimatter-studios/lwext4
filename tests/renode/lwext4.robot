# SPDX-License-Identifier: BSD-3-Clause
*** Comments ***
lwext4 on emulated microcontroller boards with an SD card on SPI.

Run with renode-test, passing the board description written by the
firmware build (build/board.env), e.g. through tests/renode/run.sh.
Every test boots the flashable image (firmware.hex), types a command on the
board's console UART and waits for the verdict; the SD card image is then
checked on the host with e2fsck/debugfs (scripts/sdimage.py).

*** Settings ***
Suite Setup         Setup
Suite Teardown      Teardown
Test Setup          Reset Emulation
Test Teardown       Test Teardown
Resource            ${RENODEKEYWORDS}
Library             OperatingSystem
Library             Process
Library             String

*** Variables ***
${BOARD}            nucleo_f401re
${BOARD_REPL}       ${EMPTY}
${BOARD_UART}       sysbus.usart2
${BOARD_SPI}        sysbus.spi1
${FIRMWARE}         ${EMPTY}
${ELF}              ${EMPTY}
${WORKDIR}          ${TEMPDIR}/lwext4-renode
${SDIMAGE}          ${CURDIR}/scripts/sdimage.py
${CARD_MIB}         32
${TEST_TIMEOUT}     1200
# Virtual time (seconds) the firmware keeps running after the torture loop
# reported its first iterations, before the power is cut.
@{CUT_DELAYS}       0.0113    0.0467    0.0891    0.1523    0.2377    0.3311

*** Keywords ***
Card Image
    [Arguments]    ${name}    @{flags}
    Create Directory    ${WORKDIR}
    ${img}=    Set Variable    ${WORKDIR}/${BOARD}-${name}.img
    ${r}=    Run Process    python3    ${SDIMAGE}    create    ${img}    ${CARD_MIB}    @{flags}
    ...    stdout=${WORKDIR}/${BOARD}-${name}-create.log    stderr=STDOUT
    Should Be Equal As Integers    ${r.rc}    0    creating ${img} failed: ${r.stdout}
    RETURN    ${img}

Host Check
    [Arguments]    ${img}    ${mode}
    ${r}=    Run Process    python3    ${SDIMAGE}    check    ${img}    ${mode}
    ...    stdout=${WORKDIR}/${BOARD}-check-${mode}.log    stderr=STDOUT
    Log    ${r.stdout}
    Should Be Equal As Integers    ${r.rc}    0    host check '${mode}' failed:\n${r.stdout}

Power Up Board
    [Arguments]    ${img}
    Execute Command    mach create "${BOARD}"
    Execute Command    machine LoadPlatformDescription @${BOARD_REPL}
    ${size}=    Get File Size    ${img}
    Execute Command    machine SdCardFromFile @${img} ${BOARD_SPI} ${size} true "sdcard"
    # The same Intel HEX file a user would flash; the ELF only adds symbols.
    Execute Command    sysbus LoadHEX @${FIRMWARE}
    Execute Command    sysbus LoadSymbolsFrom @${ELF}
    ${log}=    Replace String    ${TEST NAME}    ${SPACE}    _
    Execute Command    ${BOARD_UART} CreateFileBackend @${WORKDIR}/${BOARD}-${log}.uart.log false
    Create Terminal Tester    ${BOARD_UART}    timeout=${TEST_TIMEOUT}    defaultPauseEmulation=false
    Start Emulation
    Wait For Line On Uart    READY    timeout=60

Run Command
    [Arguments]    ${command}
    Write Line To Uart    ${command}
    ${r}=    Wait For Line On Uart    LWEXT4-TEST: (PASS|FAIL.*)    treatAsRegex=true
    Should Contain    ${r}[Line]    LWEXT4-TEST: PASS    firmware reported: ${r}[Line]
    ${mem}=    Wait For Line On Uart    MEM: .*    treatAsRegex=true    includeUnfinishedLine=false
    Log    ${BOARD}: ${mem}[Line]    console=true

Power Off Board
    # Disposes the machine, which closes (flushes) the SD card image.
    Reset Emulation

*** Test Cases ***
Mount Card Formatted On Host
    [Documentation]    mke2fs -d image with an MBR: read host files, write, remount, verify.
    [Tags]    hostimg
    ${img}=    Card Image    hostimg
    Power Up Board    ${img}
    Run Command    hostimg
    Power Off Board
    Host Check    ${img}    hostimg

Format Card On Device 1k Blocks
    [Documentation]    ext4_mbr_write + ext4_mkfs (1 KiB blocks) on a blank card.
    [Tags]    mkfs    mkfs1k
    ${img}=    Card Image    mkfs1k    --mbr-only
    Power Up Board    ${img}
    Run Command    mkfs 1024
    Power Off Board
    Host Check    ${img}    mkfs

Format Card On Device 4k Blocks
    [Documentation]    ext4_mbr_write + ext4_mkfs (4 KiB blocks) on a blank card.
    [Tags]    mkfs    mkfs4k
    ${img}=    Card Image    mkfs4k    --mbr-only
    Power Up Board    ${img}
    Run Command    mkfs 4096
    Power Off Board
    Host Check    ${img}    mkfs

Power Cut During Writes
    [Documentation]    Cut the power at several points of a create/rename/remove loop,
    ...    check e2fsck can replay what lwext4 journaled, then let lwext4 recover
    ...    the card on the next boot.
    [Tags]    powercut
    FOR    ${delay}    IN    @{CUT_DELAYS}
        Reset Emulation
        ${img}=    Card Image    powercut
        Power Up Board    ${img}
        Write Line To Uart    torture
        Wait For Line On Uart    TORTURE: iteration 2    pauseEmulation=true
        Execute Command    emulation RunFor "${delay}"
        ${pc}=    Execute Command    cpu PC
        ${sym}=    Execute Command    sysbus FindSymbolAt ${pc.strip()}
        Log    power cut after ${delay}s at ${sym.strip()}    console=true
        Power Off Board
        Copy File    ${img}    ${img}.cut
        Host Check    ${img}.cut    replay
        Power Up Board    ${img}
        Run Command    recover
        Power Off Board
        Host Check    ${img}    recover
    END
