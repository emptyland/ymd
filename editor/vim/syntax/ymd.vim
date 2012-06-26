
syntax keyword ymdContant nil true false
syntax keyword ymdKeyword typeof var and or not func with "@"

syntax keyword ymdFunc panic open read write len strbuf strcat strfin
syntax keyword ymdFunc end range rank ranki print str close insert
syntax keyword ymdFunc append match pattern argv self import env atoi
syntax keyword ymdFunc exit eval compile echo

syntax match ymdInteger "\<\d\+\>"
syntax match ymdInteger "\<\-\d\+\>"
syntax match ymdHex "\<0x\x\+\>"
syntax match ymdFix "FIX\|FIXME\|TODO"
syntax match ymdOperator "\^\|@"

syntax region ymdComment start="//" skip="\\$" end="$"
syntax region ymdString start=+L\="+ skip=+\\\\\|\\"+ end=+"+

syntax keyword ymdCondition if elif else
syntax keyword ymdLoop for while break continue
syntax keyword ymdReturn return

hi def link ymdCondition Statement
hi def link ymdLoop      Statement
hi def link ymdReturn    Statement
hi def link ymdFunc      Identifier
hi def link ymdKeyword   Type

hi ymdFix ctermfg=6 cterm=bold guifg=#0000FF
hi ymdOperator ctermfg=1
hi ymdInteger ctermfg=5
hi ymdHex ctermfg=5
hi ymdString ctermfg=5
hi ymdComment ctermfg=2
hi ymdContant ctermfg=5 guifg=#FFFF00
