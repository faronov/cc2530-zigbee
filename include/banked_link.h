/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef BANKED_LINK_H
#define BANKED_LINK_H

#if defined(CC2530_BANKED_LINK)
#if !defined(CC2530_BANKED_JOIN) || !defined(CC2530_MAC_LINK)
#error Banked LINK requires the complete banked join and interval consumer profiles
#endif
#include "banked.h"
#define LINK_FAR __banked
#else
#define LINK_FAR
#endif

#endif
