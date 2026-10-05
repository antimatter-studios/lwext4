# SPDX-License-Identifier: BSD-3-Clause
*** Comments ***
The example applications of examples/firmware (hello, datalogger, reader)
on the emulated boards, with an SD card on SPI.

Run with renode-test through tests/renode/run.sh, like lwext4.robot. Each
application prints READY and reads a line of options from the console
(platforms/sdcard/platform.c), and ends with "EXIT <status>"; the card
image and the console log are then checked on the host
(scripts/apps.py).

*** Settings ***
Suite Setup         Setup
Suite Teardown      Teardown
Test Setup          Reset Emulation
Test Teardown       Test Teardown
Resource            ${RENODEKEYWORDS}
Library             OperatingSystem
Library             Process

*** Variables ***
${BOARD}            nucleo_f401re
${BOARD_REPL}       ${EMPTY}
${BOARD_UART}       sysbus.usart2
${BOARD_SPI}        sysbus.spi1
${APP_DIR}          ${EMPTY}
${WORKDIR}          ${TEMPDIR}/lwext4-renode
${APPS}             ${CURDIR}/scripts/apps.py
${CARD_MIB}         32
${TEST_TIMEOUT}     1200
# Block writes after which the datalogger loses power: during start-up,
# at every write of a stretch of records, and late
@{CUTS}             3    100    101    102    103    104    105    106    107    108    109    110    402

*** Keywords ***
Card
    [Arguments]    ${name}    ${kind}
    Create Directory    ${WORKDIR}
    ${img}=    Set Variable    ${WORKDIR}/${BOARD}-app-${name}.img
    ${r}=    Run Process    python3    ${APPS}    create    ${img}    ${CARD_MIB}    ${kind}    stderr=STDOUT
    Should Be Equal As Integers    ${r.rc}    0    ${r.stdout}
    RETURN    ${img}

Start App
    [Arguments]    ${app}    ${img}    ${log}    ${options}
    Execute Command    mach create "${BOARD}"
    Execute Command    machine LoadPlatformDescription @${BOARD_REPL}
    ${size}=    Get File Size    ${img}
    Execute Command    machine SdCardFromFile @${img} ${BOARD_SPI} ${size} true "sdcard"
    Execute Command    sysbus LoadHEX @${APP_DIR}/${app}.hex
    Execute Command    sysbus LoadSymbolsFrom @${APP_DIR}/${app}.elf
    Remove File    ${log}
    Execute Command    ${BOARD_UART} CreateFileBackend @${log} false
    Create Terminal Tester    ${BOARD_UART}    timeout=${TEST_TIMEOUT}    defaultPauseEmulation=false
    Start Emulation
    Wait For Line On Uart    READY    timeout=60
    # Named: Renode takes an argument with '=' in it for a named one
    Write Line To Uart    content=${options}

Run App
    [Documentation]    Start an application and let it finish with EXIT 0.
    [Arguments]    ${app}    ${img}    ${log}    ${options}=${EMPTY}
    Start App    ${app}    ${img}    ${log}    ${options}
    ${r}=    Wait For Line On Uart    EXIT .*    treatAsRegex=true
    Should Contain    ${r}[Line]    EXIT 0    ${app} ended with ${r}[Line]
    # Disposes the machine, which closes (flushes) the card image
    Reset Emulation

Host Check
    [Arguments]    ${img}    ${what}    @{logs}
    ${r}=    Run Process    python3    ${APPS}    check    ${img}    ${what}    @{logs}    stderr=STDOUT
    Log    ${r.stdout}    console=true
    Should Be Equal As Integers    ${r.rc}    0    ${what}: ${r.stdout}

*** Test Cases ***
Hello
    [Documentation]    Format the card, write and read back /hello.txt.
    [Tags]    apps    hello
    ${img}=    Card    hello    blank
    Run App    hello    ${img}    ${WORKDIR}/${BOARD}-hello.uart.log
    Host Check    ${img}    hello    ${WORKDIR}/${BOARD}-hello.uart.log

Reader
    [Documentation]    Read a card made by mke2fs -d, read-only.
    [Tags]    apps    reader
    ${img}=    Card    reader    reader
    Run App    reader    ${img}    ${WORKDIR}/${BOARD}-reader.uart.log
    Host Check    ${img}    reader    ${WORKDIR}/${BOARD}-reader.uart.log

Datalogger Survives Power Cuts
    [Documentation]    Log records, cut the power at several block writes,
    ...    start again: no record reported written is lost.
    [Tags]    apps    datalogger
    ${img}=    Card    datalogger    blank
    ${log}=    Set Variable    ${WORKDIR}/${BOARD}-datalogger.uart.log
    ${cutlog}=    Set Variable    ${WORKDIR}/${BOARD}-datalogger-cut.uart.log
    Run App    datalogger    ${img}    ${log}    records=120
    Host Check    ${img}    datalogger    ${log}
    FOR    ${cut}    IN    @{CUTS}
        Start App    datalogger    ${img}    ${cutlog}    cut=${cut} records=400 trace
        Wait For Line On Uart    POWER CUT.*    treatAsRegex=true
        Reset Emulation
        Run App    datalogger    ${img}    ${log}    records=30
        Host Check    ${img}    datalogger-cut    ${cutlog}    ${log}
    END
    Host Check    ${img}    datalogger-files
