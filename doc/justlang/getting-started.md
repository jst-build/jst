# Justlang: Getting Started

## Introduction

Justlang is a feature-rich expression language for the `jst` build system.
It is a dynamically-typed functional language with a *[Jsonnet](https://jsonnet.org)-like* syntax.
In its core, it is JSON with local variables/functions, control-flow statements,
and a set of [standard library](./stdlib.md) functions.


> Note: Justlang is currently only supported for `TARGETS` files (not `RULES`
> nor `EXPRESSIONS`).

## Truth value

The *truth value* used when casting to Boolean (e.g., conditionals) is
considered to be `true` for every non-negative value. Negative values are:
`null`, `false`, `0`, `''`, `[]`, and `{}`

## Syntactical constructs

### Boolean operators

The following binary Boolean operators are supported: `==`, `!=` `&&`, and `||`.
Furthermore, the unary Boolean operator `!` is supported as well. For
non-Boolean values, casting is applied according to the [*truth value*](#truth-value) above.

``` jsonnet
'123' == 123;   // false
'123' != 123;   // true
'yes' && [];    // false
'yes' || [];    // true
!'yes';         // false
```

### Arithmetic operators

The following binary arithmetic operators are supported: `+`, `-`, and `*`.
Furthermore, the unary arithmetic operator `-` is supported as well.

``` jsonnet
1 + 2;  //  3.0
1 - 2;  // -1.0
2 * 2;  //  4.0
1 + -2; // -1.0
2 * -2; // -4.0
```

### Composition operator

The composition operator `+` supports concatenating strings, combining lists,
and computing the union of maps. The union of maps is computed non-disjointly,
i.e., the last value wins in the case of conflicting keys.

``` jsonnet
'foo' + 'bar';                  // 'foobar'
['foo'] + ['bar'];              // ['foo', 'bar']
{foo: 'FOO'} + {bar: 'BAR'};    // {foo: 'FOO', bar: 'BAR'}
```

### If statements

Control flow can be added by using `if` statements, which are of the form
`if <expr> then <expr> [else <expr>]`. The `else` branch is optional, and will
produce the empty list `[]` if not specified for negative conditions.

``` jsonnet
if /*cond*/ then 'pass' else 'fail';    // 'pass' or 'fail'
if /*cond*/ then 'pass';                // 'pass' or []
```

### Variables and functions

Justlang supports the definition of variables and functions using the `local`
keyword. Every definition can access previous definitions in its *scope*.
Functions may have parameters. Parameters with default values are considered to
be optional.

``` jsonnet
local foo = 'foo';                      // variable: foo
local bar(x, y='baz') = [foo, x, y];    // function: bar(x, y)
bar('bar');                             // produces: ['foo', 'bar', 'baz']
```

### List lookup

List elements can be accessed up using the `[]`-operator with the position as
number. Negative numbers count backwards from the list's end. String arguments
will be converted to number, or considered to be `0` if the conversion fails.

``` jsonnet
local list = ['foo', 'bar'];
list[0];    // 'foo'
list[-1];   // 'bar'
```

### Map lookup

Map values can be accessed using the operators `.` or `[]`, depending on their
keys. Key names that satisfy the requirements of identifiers can use the
`.`-operator, while all other keys must use the `[]`-operator with the full key
name as string.

``` jsonnet
local map = {foo: 'FOO', 'bar baz': 'BAR BAZ'};
map.foo;        // 'FOO'
map['bar baz']; // 'BAR BAZ'
```

### Computed fields

Fields (key names) can also be computed using the `[<expr>]` syntax. The
expression `<expr>` can be any arbitrary computation that must produce a string.

``` jsonnet
{['foo' + 'bar']: 'baz'};   // {foobar: 'baz'}
```

### List comprehension

List can be computed from other lists (ranges) using list comprehension of the
form `[<expr> for <id> in <expr>]`. The second `<expr>` must evaluate to a list.
The syntax is similar to Python's list comprehension.

``` jsonnet
['x' + i for i in ['foo', 'bar']];  // ['xfoo', 'xbar']
```

### Map comprehension

Maps can be computed from lists (ranges) using map comprehension of the form
`{[<expr>]:<expr> for <id> in <expr>}`. The last `<expr>` must evaluate to a
list. The key name is computed using the syntax for [computed fields](#computed-fields).
The overall syntax is similar to Python's dictionary comprehension.

``` jsonnet
{['k' + x]:'v' + x for x in ['A', 'B']};    // {kA: 'vA', kB: 'vB'}
```

### Multi-line strings

Using the operator `|||`, strings that contain multiple lines can be specified,
without the need to quote specific characters. The multi-line string ends when
this operator is put at the beginning (first non-blank character) of a new line.
All string content must be indented by at least one space more than the closing
operator. The result is a JSON-encoded string with all lines separated by the
newline character `\n`.

``` jsonnet
local foo = |||
  set -eu
  if [ -n "$1" ]; then
    echo "foo"
  fi
|||;
// produces: "set -eu\nif [ -n \"$1\" ]; then\n  echo \"foo\"\nfi\n"
```

### Imports

With the `import` keyword static data can be imported from other files within
the same repository file root. Relative paths are always considered to be
relative to the current file, while absolute paths are considered to be relative
to the workspace root of the current repository.

``` jsonnet
local foo = import '/foo.jst';  // contains: {bar: 'BAR', baz(): 'BAZ'}
foo.bar;    // 'BAR'
foo.baz();  // 'BAZ'
```

### References

References to entities (targets, rules, etc.) are represented by the syntactical
construct `@'<ref>'`. The encoding for `<ref>` is explained in section
[Reference encoding](#reference-encoding).

``` jsonnet
{
  ALL: {
    type: 'install',
    deps: [
      @':README.md',        // local reference
      @'./bin:main',        // relative reference
      @'//doc:html',        // absolute reference
      @'ext//lib:shared',   // external reference
    ],
  }
}
```

More examples how to address targets are provided in section [Target naming](../../doc/concepts/overview.md#target-naming).

## Reference encoding

There are three types of target references.

### Local references

A local target reference is of the form:  
$\qquad$ **`:`***`<target>`*

The *`<target>`* segment may be specified in double-quotes, containing a
JSON-encoded string.

> Note: local references can also be specified via their short-hand form:  
> the target name as plain string `'<target>'`, instead of `@':<target>'`.

### Relative references

Relative references contain two segments and are of the form:  
$\qquad$ **`./`***`<submodule>`***`:`***`<target>`*

Every segment may be specified in double-quotes, containing a JSON-encoded
string. Omitting **`:`***`<target>`* refers to the basename of the submodule.

Examples:

- `@'./foo/bar:baz'`  
  Reference to target `baz` in submodule `foo/bar`, relative to local module
- `@'./foo/bar'`  
  Reference to target `bar` in submodule `foo/bar`, relative to local module

### Absolute references

Absolute target reference contain three segments and are of the form:  
$\qquad$ *`<repo>`***`//`***`<module>`***`:`***`<target>`*

Every segment may be specified in double-quotes, containing a JSON-encoded
string. The following rules apply:

- omitting *`<repo>`* refers to the local repository.
- omitting *`<module>`* refers to the top-level module.
- omitting **`:`***`<target>`* refers to the basename of the module.

Examples:

- `@'//:baz'`  
  Reference to target `baz` in the top-level module from the local repository
- `@'//foo/bar:baz'`  
  Reference to target `baz` in module `foo/bar` from the local repository
- `@'//foo/bar'`  
  Reference to target `bar` in module `foo/bar` from the local repository
- `@'rules//CC/auto:config_file'`  
  Reference to target `config_file` in module `CC/auto` from repository `rules`
