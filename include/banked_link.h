/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef BANKED_LINK_H
#define BANKED_LINK_H

#include "cc2530_mmio.h"

#if defined(CC2530_BANKED_LINK)
#if !defined(CC2530_BANKED_JOIN) || !defined(CC2530_MAC_LINK)
#error Banked LINK requires the complete banked join and interval consumer profiles
#endif
#if defined(CC2530_CLANG)
/* LLVM MCS-51 owns bank placement via its generated bank-call thunks. */
#define LINK_FAR
#else
#include "banked.h"
#define LINK_FAR __banked
#endif
#else
#define LINK_FAR
#endif

#endif
