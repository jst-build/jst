# Justlang: Troubleshoot

## Manual evaluation

For testing purposes, the Justlang evaluation can be run manually with the
subcommand `jst eval`. As input, it accepts Justlang code from a file path or
from stdin (via `-`):

``` sh
$ echo "'foo' + jst.env('BAR', default='')" > snippet.jst
$ jst eval snippet.jst
"foo"
```

## Evaluate runtime variables

Similar to `jst build`, runtime variables for evaluation are provided as a JSON
object. This object is read either from file via the option `-c,--config`, or
from inline JSON on the command line via the option `-D,--defines`:

``` sh
$ jst eval -D'{"BAR":"bar"}' snippet.jst
"foobar"
```

## Generate Justbuild IR

Advanced users might want to investigate the generated Justbuild IR (JSON),
which can be requested by specifying the flag `--ir`:

``` sh
$ jst eval --ir snippet.jst
{
  "type": "join",
  "$1": [
    "foo",
    {
      "type": "var",
      "name": "BAR",
      "default": ""
    }
  ]
}
```

> Note: For `TARGETS`, `RULES`, and `EXPRESSIONS` files, use the flags
> `--targets`, `--rules`, and `--expressions`, respectively. All of them already
> imply the `--ir` flag.

## Static type checks

Static type errors will be detected immediately and reported with a detailed
log message:

``` sh
$ echo "'foo' + 42 + 'bar'" > snippet.jst
$ jst eval snippet.jst
ERROR: AST inlining failed with:
       snippet.jst:1:6-11: Unsupported types for binary operation. Found String and Number.

       1 |  'foo' + 42 + 'bar'
         |       ^^^^^^

ERROR: Parsing input file failed.
```
