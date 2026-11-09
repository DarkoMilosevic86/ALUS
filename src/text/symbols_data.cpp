// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// The compiled-in symbol floor.
//
// `data/symbols/en.dic` is the real English dictionary and is what an installed
// ALUS reads. This is the minimum that is true even when nothing is installed:
// ASCII punctuation and whitespace, no complex symbols. Without it, a broken or
// missing data directory would make ALUS read punctuation as silence, which is
// the kind of failure a blind user cannot diagnose.
#include "alus/text/symbols.hpp"

namespace alus::text {

std::string_view builtinEnglishSymbols() {
    return R"ALUS(symbols:
\0	blank	char	-	# null
\t	tab
\n	line feed	char
\r	carriage return	char
 	space	char
!	bang	all
"	quote	most
\#	number	some
$	dollar	all	norep
%	percent	some
&	and	some
'	tick	all
(	left paren	most
)	right paren	most
*	star	some
+	plus	some
,	comma	all	always
-	dash	most
.	dot	all
/	slash	some
:	colon	most
;	semi	most
<	less	most
=	equals	some
>	greater	most
?	question	all
@	at	some
[	left bracket	most
\\	backslash	most
]	right bracket	most
^	caret	most
_	line	most
`	graav	most
{	left brace	most
|	bar	most
}	right brace	most
~	tilde	most
)ALUS";
}

}  // namespace alus::text
