#ifndef U2_ATTR_H
#define U2_ATTR_H

/*
 * attr.h — the one spelling for "keep this, and do not warn that it is unused".
 *
 * The engine deliberately keeps a few things that no caller references, because
 * their job is to name the original opcode a behaviour comes from rather than to
 * run. game/commerce.c's bcd_inc1 is the model: every increment site goes through
 * u2_bcd_inc_sat, and bcd_inc1 stays so the DAA instruction the rule comes from
 * has a home. Without a marker, -Wall is simply wrong about those.
 *
 * [[maybe_unused]] is C23. The engine is C11 — the floor _Static_assert forces,
 * and not gnu11 or C++ because the release constraint is a plain cc with no
 * dependency resolution — so the double-underscore GNU form is used instead. gcc
 * and clang both accept it, and unlike [[...]] it draws no -Wpedantic diagnostic
 * in C11 mode. The fallback is empty: an unknown compiler then gets an unused
 * warning, which is a nuisance, not a build failure.
 */

#if defined(__GNUC__) || defined(__clang__)
# define U2_MAYBE_UNUSED __attribute__((unused))
#else
# define U2_MAYBE_UNUSED
#endif

#endif /* U2_ATTR_H */
