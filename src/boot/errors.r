REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Standard Error IDs and Display Templates"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This specifies error categories and IDs that are given SYM_XXX
        constants and can be evoked using those numbers by C code.

        The errors map to either a TEXT! string, or a BLOCK! with GET-WORD!
        slots showing where argument substitution will go.  Traditionally
        these arguments were named :arg1, :arg2, :arg3...however an idea
        emerging is that error contexts will ultimately use their data
        members to be meaningfully-named arguments for that error ID.

        As a convenience for invoking the errors correctly from C with the
        right number of arguments, see the Error_Xxx_Yyy_Raw() functions
        auto-generated by %make-boot.r.

        Because error callsites only name the ID and category of the error,
        the messages are stored in an "errors catalog".  For this reason,
        standard errors evoked from usermode as well as C are found here.

        !!! TBD: method of extending the error catalog and managing the
        ecology of identity of new errors (possibly via URL uniqueness).

        !!! TBD: method of cleaning up errors not referenced from the code.
    }
]


Internal: [
    ; !!! Should there be a distinction made between different kinds of
    ; stack overflows?  (Call stack, Data stack?)
    ;
    stack-overflow:     {stack overflow}

    not-done:           {reserved for future use (or not yet implemented)}

    no-memory:          [{not enough memory:} :arg1 {bytes}]

    io-error:           {problem with IO}
    locked-series:      {locked series expansion}
    unexpected-case:    {no case in switch statement}
    bad-path:           [{bad path:} :arg1]
    not-here:           [:arg1 {not supported on your system}]
    globals-full:       {no more global variable space}
    bad-sys-func:       [{invalid or missing system function:} :arg1]
    invalid-error:      [{error object or fields were not valid:} :arg1]
    hash-overflow:      {Hash ran out of space}

    bad-utf8:           {invalid UTF-8 byte sequence found during decoding}
    codepoint-too-high: [{codepoint} :arg1 {too large (or data is not UTF-8)}]
    illegal-zero-byte:  {#{00} bytes illegal in ANY-STRING!, use BINARY!}
    illegal-cr:         [{Illegal CR: See DELINE, and TO-TEXT/RELAX:} :arg1]
    mixed-cr-lf-found:  {DELINE requires files to be CR LF or LF consistently}

    debug-only:         {Feature available only in DEBUG builds}

    invalid-exit:       {Frame does not exist on the stack to EXIT from}
]

Syntax: [
    scan-invalid:       [{invalid} :arg1 {--} :arg2]
    scan-missing:       [{missing} :arg1]
    scan-extra:         [{extra} :arg1]
    scan-mismatch:      [{expected} :arg1 {but got} :arg2]

    no-header:          [{script is missing a REBOL header:} :arg1]
    bad-header:         [{script header is not valid:} :arg1]
    bad-compress:       [{compressed script body is not valid:} :arg1]
    malconstruct:       [{invalid construction spec:} :arg1]
    bad-char:           [{invalid character in:} :arg1]
    needs:              [{this script needs} :arg1 :arg2 {or better to run correctly}]
]

Script: [
    no-value:           [:arg1 {has no value}]
    need-non-void:      [:arg1 {is VOID!}]
    need-non-null:      [:arg1 {needs a value, can't be null}]
    need-non-end:       [{end was reached while trying to set} :arg1]
    not-bound:          [:arg1 {word is not bound to a context}]
    no-relative:        [:arg1 {word is bound relative to context not on stack}]
    not-in-context:     [:arg1 {is not in the specified context}]

    void-evaluation:    "VOID! cells cannot be evaluated"

    assertion-failure:  [{assertion failure:} :arg1]

    phase-bad-arg-type:
        [:arg1 {internal phase disallows} :arg2 {for its} :arg3 {argument}]
    phase-no-arg:
        [:arg1 {internal phase requres} :arg2 {argument to not be null}]

    expect-val:         [{expected} :arg1 {not} :arg2]
    expect-type:        [:arg1 :arg2 {field must be of type} :arg3]
    cannot-use:         [{cannot use} :arg1 {on} :arg2 {value}]

    ambiguous-infix:    {Ambiguous infix expression--use GROUP! to clarify}
    literal-left-path:  {Use -> to pass literal left PATH! parameters right}

    bad-get-group:      {GET-GROUP! gets WORD!/PATH!/BLOCK!, arity-0 ACTION!}
    bad-set-group:      {SET-GROUP! sets WORD!/PATH!/BLOCK!, arity-1 ACTION!}

    do-running-frame:   [{Must COPY a FRAME! that's RUNNING? before DOing it}]
    expired-frame:      [{Cannot use a FRAME! whose stack storage expired}]

    apply-too-many:     {Too many values in processed argument block of APPLY.}

    print-needs-eval:   {PRINT needs /EVAL to process non-literal blocks}

    hijack-blank:       {Hijacked function was captured but no body given yet}

    evaluate-null:      {null cannot be evaluated (see UNEVAL)}

    enfix-path-group:   [:arg1 {GROUP! can't be in a lookback quoted PATH!}]
    evaluative-quote:   {Can't quote non-literal from an evaluative source}

    break-not-continue: {Use BREAK/WITH when body is the breaking condition}

    use-eval-for-eval:  {Use EVAL or APPLY on actions of arity > 0, not DO}

    limited-fail-input: {FAIL requires complex expressions to be in a GROUP!}

    ; BAD-VALUE is the laziest error with an argument.  BAD-ARGUMENT now
    ; tells you what the parameter of the argument was for.

    unknown-error:      {Unknown error (failure on null, no additional info)}
    bad-value:          [{Failure on bad value (no additional info):} :arg1]

    invalid-arg:        [:arg1 {has an invalid} :arg2 {argument:} :arg3]
    no-arg:             [:arg1 {is missing its} :arg2 {argument}]
    expect-arg:         [:arg1 {does not allow} :arg2 {for its} :arg3 {argument}]
    arg-required:       [:arg1 {requires} :arg2 {argument to not be null}]

    invalid-type:       [:arg1 {type is not allowed here}]
    invalid-op:         [{invalid operator:} :arg1]
    no-op-arg:          [:arg1 {operator is missing an argument}]
    invalid-data:       [{data not in correct format:} :arg1]
    not-same-type:      {values must be of the same type}
    not-related:        [{incompatible argument for} :arg1 {of} :arg2]
    bad-func-def:       [{invalid function definition:} :arg1]
    bad-func-arg:       [{function argument} :arg1 {is not valid}]

    needs-return-opt:   [:arg1 {can't return null (see RETURN: [<opt> ...])}]
    needs-return-value: [:arg1 {can't return void! (see RETURN: <void>)}]
    bad-return-type:    [:arg1 {doesn't have RETURN: enabled for} :arg2]

    no-refine:          [:arg1 {has no refinement called} :arg2]
    bad-refines:        {incompatible or invalid refinements}
    bad-refine:         [{incompatible or duplicate refinement:} :arg1]
    non-logic-refine:   [:arg1 {refinement must be LOGIC!, not} :arg2]

    legacy-refinement:  [
                            {Refinements now act as their own args.  See}
                            {https://trello.com/c/DaVz9GG3 - spec was} :arg1
                        ]
    legacy-local:       [
                            {/LOCAL is a plain refinement now, use <local>}
                            {https://trello.com/c/IyxPieNa - spec was} :arg1
                        ]

    bad-field-set:      [{cannot set} :arg1 {field to} :arg2 {datatype}]
    bad-path-pick:      [{cannot pick} :arg1 {in path}]
    bad-path-poke:      [{cannot poke} :arg1 {in path}]
    dup-vars:           [{duplicate variable specified:} :arg1]

    past-end:           {out of range or past end}
    missing-arg:        {missing a required argument or refinement}
    too-short:          {content too short (or just whitespace)}
    too-long:           {content too long}
    invalid-chars:      {contains invalid characters}
    invalid-compare:    [{cannot compare} :arg1 {with} :arg2]

    invalid-part:       [{invalid /part count:} :arg1]

    no-return:          {block did not return a value}

    ; !!! Consider enhancements which would allow suppressing the /NAME in the
    ; rendering if not present.
    ;
    no-catch:           [{No CATCH for THROW of} :arg1 {with /NAME:} :arg2]

    bad-bad:            [:arg1 {error:} :arg2]

    bad-make-parent:    [{cannot MAKE} :arg1 {with parent} :arg2]
    bad-make-arg:       [{cannot MAKE/TO} :arg1 {from:} :arg2]
    wrong-denom:        [:arg1 {not same denomination as} :arg2]
;   bad-convert:        [{invalid conversion value:} :arg1]
    bad-compression:    [{invalid compressed data - problem:} :arg1]
    dialect:            [{incorrect} :arg1 {dialect usage at:} :arg2]
    bad-command:        {invalid command format (extension function)}
    bad-cast:           [{cannot cast} :arg1 {as} :arg2]
    alias-constrains:   [{AS constrains unlocked input, so must be mutable}]

    return-archetype:   {RETURN called with no generator providing it in use}

    parse-rule:         {PARSE - invalid rule or usage of rule}
    parse-end:          {PARSE - unexpected end of rule}
    parse-variable:     [{PARSE - expected a variable, not:} :arg1]
    parse-command:      [{PARSE - command cannot be used as variable:} :arg1]
    parse-series:       [{PARSE - input must be a series:} :arg1]

    bad-library:        {bad library (already closed?)}
    only-callback-ptr:  {Only callback functions may be passed by FFI pointer}
    free-needs-routine: {Function to destroy struct storage must be routine}

    block-skip-wrong:   {Block is not even multiple of skip size}

    frame-already-used: [{Frame currently in use by a function call} :arg1]
    frame-not-on-stack: {Frame is no longer running on the stack}

    varargs-no-stack:   {Call originating VARARGS! has finished running}
    varargs-make-only:  {MAKE *shared* BLOCK! supported on VARARGS! (not TO)}
    varargs-no-look:    {VARARGS! may only lookahead by 1 if "hard quoted"}
    varargs-take-last:  {VARARGS! does not support TAKE-ing only /LAST item}

    null-vararg-array:  {Can't MAKE ANY-ARRAY! from VARARGS! that allow <opt>}
    null-object-block:  {Can't create block from object if it has null values}

    conflicting-key:    [:arg1 {key conflicts; use SELECT or PUT with /CASE}]

    block-conditional:  [{Literal block used as conditional} :arg1]
    void-conditional:   [{VOID! values are not conditionally true or false}]
    non-block-branch:   [{Evaluated non-block/function used as branch} :arg1]
    block-switch:       [{Literal block used as switch value} :arg1]

    native-unloaded:    [{Native has been unloaded:} :arg1]
]

Math: [
    zero-divide:        {attempt to divide by zero}
    overflow:           {math or number overflow}
    positive:           {positive number required}

    type-limit:         [:arg1 {overflow/underflow}]
    size-limit:         [{maximum limit reached:} :arg1]
    out-of-range:       [{value out of range:} :arg1]
]

Access: [
    protected-word:     [{variable} :arg1 {locked by PROTECT (see UNPROTECT)}]

    const-value:        [{CONST or iterative value (see MUTABLE):} :arg1]

    series-protected:   {series read-only due to PROTECT (see UNPROTECT)}
    series-frozen:      {series is source or permanently locked, can't modify}
    series-held:        {series has temporary read-only hold for iteration}
    series-auto-locked: {series was implicitly locked (e.g. as key for MAP!)}

    series-data-freed:  {series contents no longer available due to FREE}

    hidden:             {not allowed - would expose or modify hidden values}

    cannot-open:        [{cannot open:} :arg1 {reason:} :arg2]
    not-open:           [{port is not open:} :arg1]
    already-open:       [{port is already open:} :arg1]
;   already-closed:     [{port} :arg1 {already closed}]
    no-connect:         [{cannot connect:} :arg1 {reason:} :arg2]
    not-connected:      [{port is not connected:} :arg1]
;   socket-open:        [{error opening socket:} :arg1]
    no-script:          [{script not found:} :arg1]

    no-scheme-name:     {Scheme has no `name:` field (must be WORD!)}
    no-scheme:          [{missing port scheme:} :arg1]

    invalid-spec:       [{invalid spec or options:} :arg1]
    invalid-port:       [{invalid port object (invalid field values)}]
    invalid-actor:      [{invalid port actor (must be native or object)}]
    invalid-port-arg:   [{invalid port argument:} :arg1]
    no-port-action:     [{this port does not support:} :arg1]
    protocol:           [{protocol error:} :arg1]
    invalid-check:      [{invalid checksum (tampered file):} :arg1]

    write-error:        [{write failed:} :arg1 {reason:} :arg2]
    read-error:         [{read failed:} :arg1 {reason:} :arg2]
    read-only:          [{read-only - write not allowed:} :arg1]
    timeout:            [{port action timed out:} :arg1]

    no-create:          [{cannot create:} :arg1]
    no-delete:          [{cannot delete:} :arg1]
    no-rename:          [{cannot rename:} :arg1]
    bad-file-path:      [{bad file path:} :arg1]
    bad-file-mode:      [{bad file mode:} :arg1]
;   protocol:           [{protocol error} :arg1]

    security:           [{security violation:} :arg1 { (refer to SECURE function)}]
    security-level:     [{attempt to lower security to} :arg1]
    security-error:     [{invalid} :arg1 {security policy:} :arg2]

    no-codec:           [{cannot decode or encode (no codec):} :arg1]
    bad-media:          [{bad media data (corrupt image, sound, video)}]
;   would-block:        [{operation on port} :arg1 {would block}]
;   no-action:          [{this type of port does not support the} :arg1 {action}]
;   serial-timeout:     {serial port timeout}
    no-extension:       [{cannot open extension:} :arg1]
    bad-extension:      [{invalid extension format:} :arg1]
    extension-init:     [{extension cannot be initialized (check version):} :arg1]

    symbol-not-found:   [{symbol not found:} :arg1]
    bad-memory:         [{non-accessible memory at} :arg1 {in} :arg2]
    no-external-storage: [{no external storage in the series}]
    already-destroyed:  [{storage at} :arg1 {already destroyed}]
]
