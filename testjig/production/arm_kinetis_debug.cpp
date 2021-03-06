/*
 * Simple ARM debug interface for Arduino, using the SWD (Serial Wire Debug) port.
 * Extensions for Freescale Kinetis chips.
 * 
 * Copyright (c) 2013 Micah Elizabeth Scott
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <Arduino.h>
#include "arm_kinetis_debug.h"
#include "arm_kinetis_reg.h"


bool ARMKinetisDebug::startup()
{
    uint32_t idr;
    uint32_t status;

    // System resets can be slow, give them more time than the default.
    const unsigned resetRetries = 2000;

    // Make sure we're on a compatible chip. The MDM-AP peripheral is Freescale-specific.
    if (!apRead(REG_MDM_IDR, idr))
        return false;
    if (idr != 0x001C0000) {
        log(LOG_ERROR, "ARMKinetisDebug: Didn't find a supported MDM-AP peripheral");
        return false;
    }

    // Put the control register in a known state, and make sure we aren't already in the middle of a reset
    if (!apWrite(REG_MDM_CONTROL, REG_MDM_CONTROL_CORE_HOLD_RESET))
        return false;
    if (!apReadPoll(REG_MDM_STATUS, status, REG_MDM_STATUS_SYS_NRESET, -1, resetRetries))
        return false;

    // System reset
    if (!apWrite(REG_MDM_CONTROL, REG_MDM_CONTROL_SYS_RESET_REQ))
        return false;
    if (!apReadPoll(REG_MDM_STATUS, status, REG_MDM_STATUS_SYS_NRESET, 0))
        return false;
    if (!apWrite(REG_MDM_CONTROL, 0))
        return false;

    // Re-initialize the AHB-AP after reset
    if (!initMemPort())
        return false;

    // Wait until the flash controller is ready & system is out of reset
    if (!apReadPoll(REG_MDM_STATUS, status, REG_MDM_STATUS_SYS_NRESET | REG_MDM_STATUS_FLASH_READY, -1, resetRetries))
        return false;

    // Enable debugging
    if (!memStore(REG_SCB_DHCSR, 0xA05F0001))
        return false;

    // Halt the CPU core. Keep trying, in case we're fighting with watchdog reset :(
    unsigned retries = 50;
    uint32_t dhcsr;
    do {
        retries--;

        // Request a halt, and read back status
        if (!memStore(REG_SCB_DHCSR, 0xA05F0003))
            return false;
        if (!memLoad(REG_SCB_DHCSR, dhcsr))
            return false;
    
        // Wait for S_HALT acknowledgment bit
    } while (!(dhcsr & (1 << 17)) && retries);
    if (!retries) {
        log(LOG_ERROR, "ARMKinetisDebug: Failed to put CPU in debug halt state. (DHCSR: %08x)", dhcsr);
        return false;
    }

    // Enable peripheral clocks
    if (!memStore(REG_SIM_SCGC5, 0x00043F82))
        return false;
    if (!memStore(REG_SIM_SCGC6, REG_SIM_SCGC6_FTM0 | REG_SIM_SCGC6_FTM1 | REG_SIM_SCGC6_FTFL))
        return false;

    // Test AHB-AP: Can we successfully write to RAM?
    if (!memStoreAndVerify(0x20000000, 0x31415927))
        return false;
    if (!memStoreAndVerify(0x20000000, 0x76543210))
        return false;

    // Good to go!
    return true;
}

bool ARMKinetisDebug::flashMassErase()
{
    // Erase all flash, even if some of it is protected.

    uint32_t status;
    if (!apRead(REG_MDM_STATUS, status))
        return false;
    if (!(status & REG_MDM_STATUS_FLASH_READY)) {
        log(LOG_ERROR, "FLASH: Flash controller not ready before mass erase");
        return false;
    }
    if ((status & REG_MDM_STATUS_FLASH_ERASE_ACK)) {
        log(LOG_ERROR, "FLASH: Mass erase already in progress");
        return false;
    }
    if (!(status & REG_MDM_STATUS_MASS_ERASE_ENABLE)) {
        log(LOG_ERROR, "FLASH: Mass erase is disabled!");
        return false;
    }

    log(LOG_NORMAL, "FLASH: Beginning mass erase operation");
    if (!apWrite(REG_MDM_CONTROL, REG_MDM_CONTROL_CORE_HOLD_RESET | REG_MDM_CONTROL_MASS_ERASE))
        return false;

    // Wait for the mass erase to complete
    if (!apReadPoll(REG_MDM_STATUS, status, REG_MDM_STATUS_FLASH_ERASE_ACK, 0, 10000)) {
        log(LOG_ERROR, "FLASH: Timed out waiting for mass erase to complete");
        return false;
    }

    if (!(status & REG_MDM_STATUS_FLASH_READY)) {
        log(LOG_ERROR, "FLASH: Flash controller not ready after mass erase");
        return false;
    }

    return true;
}
