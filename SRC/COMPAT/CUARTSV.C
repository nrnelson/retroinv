/*
 * CUARTSV.C - the serial port calls a client never makes (COMPAT.H):
 * the bytes waiting, dropping them, the modem lines, and the port's
 * chip and counts, which the server and COMTEST show.
 *
 * They are a module of their own so that a client, which never calls
 * them, does not link them; they work on CUART.C's ports through the
 * internals of CUART.H.
 */
#define COMPAT_INTERNAL
#define COMPAT_UART
#include "COMPAT.H"

#ifdef COMPAT_HOST

#include <string.h>

u16 uart_rxcount(u8 port)
{
    (void)port;
    return 0;
}

void uart_rxflush(u8 port)
{
    (void)port;
}

u8 uart_modem(u8 port)
{
    (void)port;
    return 0;
}

void uart_getinfo(u8 port, uart_info *i)
{
    (void)port;
    memset(i, 0, sizeof *i);
}

#else

#include <stddef.h>
#include <string.h>
#include "CUART.H"

u16 uart_rxcount(u8 port)
{
    cu_port *p;
    FARP(volatile cu_vars) v;

    p = cuart_polled(port);
    if (p == NULL)
        return 0;
    v = CU_VARS(p);
    return CU_RXHELD(v);
}

void uart_rxflush(u8 port)
{
    cu_port *p;

    p = cuart_port(port);
    if (p == NULL)
        return;
    cuart_take(p, NULL, 0, UART_RXSZ);
    cuart_serve(p);
    cuart_take(p, NULL, 0, UART_RXSZ);
}

u8 uart_modem(u8 port)
{
    cu_port *p;
    u16 fl;
    u8 m;

    p = cuart_port(port);
    if (p == NULL)
        return 0;
    fl = IRQ_SAVE();
    IRQ_OFF();
    m = (u8)CU_IN(CU_VARS(p)->info.base + UR_MSR);
    cuart_serve(p);
    if (fl & IRQ_IF)
        IRQ_ON();
    return (u8)(m & (UART_M_CTS | UART_M_DSR | UART_M_DCD));
}

void uart_getinfo(u8 port, uart_info *i)
{
    cu_port *p;
    u16 fl;

    p = cuart_port(port);
    if (p == NULL) {
        memset(i, 0, sizeof *i);
        return;
    }
    fl = IRQ_SAVE();
    IRQ_OFF();
    cmem_get(p->b, (u16)(p->skew + UB_BASE), i, sizeof *i);
    if (fl & IRQ_IF)
        IRQ_ON();
}

#endif
