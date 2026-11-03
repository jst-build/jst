Jstlang Expression Language
===========================

In order to define a rule, we need a
restricted form of functional computation. This is achieved by our
expression language, Jstlang, which is used for `TARGETS` and `RULES`
files alike.

Semantic Values
---------------

Expressions evaluate to semantic values. Semantic values are essentially JSON
values extended by additional atomic values for build-internal values like
artifacts, names, etc.

### Truth value

Every value can be treated as a boolean condition. We follow a
convention similar to `LISP` considering everything true that is not
empty.

These values are logically false: `null`, `false`, `0`, `''`,
`[]`, and `{}`

All other values are logically true.

Syntax
------

Jstlang is a dynamically-typed functional language with a
*[Jsonnet](https://jsonnet.org)-like* syntax. In its core, it is JSON
with local variables/functions, control-flow statements, and a set of
[standard library](#regular-functions) functions.

### Boolean operators

The following binary Boolean operators are supported: `==`, `!=` `&&`, and `||`.
Furthermore, the unary Boolean operator `!` is supported as well. For
non-Boolean values, casting is applied according to the [*truth value*](#truth-value)
above. The operators `&&` and `||` use short-cut evaluation, see
[conjunction and disjunction](#conjunction-and-disjunction-jstallargs-and-jstanyargs).

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
{foo: 'FOO'} + {bar: 'BAR'};    // {bar: 'BAR', foo: 'FOO'}
```

### If statements

Control flow can be added by using `if` statements, which are of the form
`if <expr> then <expr> [else <expr>]`. The `else` branch is optional, and will
produce the empty list `[]` if not specified for negative conditions.

``` jsonnet
if $'CONDITION' then 'pass' else 'fail';    // 'pass' or 'fail'
if $'CONDITION' then 'pass';                // 'pass' or []
```

First the condition is evaluated. If it evaluates to a value that
is logically true, then the `then` branch is evaluated and its value is
the result of the evaluation. Otherwise, the `else` branch (if present,
otherwise `[]` is taken as default) is evaluated and the obtained value
is the result of the evaluation.

Conditionals can be chained as `if <cond> then <expr> else if <cond>
then <expr> ... else <expr>`. In the order of the chain, the conditions
are evaluated, until one evaluates to a value that is logically true.
For that condition, the corresponding expression is evaluated and the
result of this evaluation is the result of the whole expression. This
also serves as case distinction.

``` jsonnet
if $'OS' == 'linux' then 'elf'
else if $'OS' == 'darwin' then 'mach-o'
else 'unknown'
```

### Variables and functions

Jstlang supports the definition of variables and functions using the `local`
keyword. Every definition can access previous definitions in its *scope*.
Functions may have parameters. Parameters with default values are considered to
be optional.

``` jsonnet
local foo = 'foo';                      // variable: foo
local bar(x, y='baz') = [foo, x, y];    // function: bar(x, y)
bar('bar');                             // produces: ['foo', 'bar', 'baz']
```

The `local` definitions are evaluated in the order they occur. After
each evaluation, a new environment is taken for the subsequent
evaluations; the new environment is like the old one but amended at the
position given by the name of the definition to now map to the value
just obtained. Finally, the body is evaluated in the final environment
(after evaluating all definitions) and the result of evaluating the
body is the value for the whole expression.

The default value of a parameter is evaluated in the scope of the function's
definition, extended by the *preceding* parameters (but not by the parameter
itself or any following ones). Hence, a default value can refer to an earlier
parameter, and a parameter can default to a variable of the same name from the
enclosing scope.

``` jsonnet
local foo = 'foo';
local func(foo=foo, foobar=foo+'bar') = foobar;
func();         // produces: 'foobar'
func('FOO');    // produces: 'FOObar'
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

Files of other repositories are imported by a file-reference
`@'<repo>//<path>'`, with `<repo>` being a name the current repository binds
to the other repository (as for target references), and the path being relative
to the root of the other repository. The file is read from the root of the same
kind as the current file, e.g., a `RULES` file imports from the rule root of
the other repository.

``` jsonnet
local common = import @'rules//expressions.jst';
```

### Runtime Variables

Runtime variables (typically set via `--defines` or `--config`) can be read via
the syntactical construct `$<var>`, with `<var>` being the name of the
variable. This short form requires the name to be a valid identifier (letters,
digits, and underscores, not starting with a digit); the variable name ends at
the first character that is not part of an identifier. Variables with any other
name can be read via the quoted form `$'<var>'`.

``` jsonnet
if $DEBUG
then 'DEBUG variable is set with positive truth value'
else 'DEBUG variable is not set or contains negative truth value'
// Note: Use $'my-var' for variable names that are not valid identifiers
```

### References

References to entities (targets, rules, etc.) are represented by the syntactical
construct `@'<ref>'`. The encoding of `<ref>` and its meaning are explained in
section [Target naming](overview.md#target-naming) of the *Tool Overview*
documentation.

``` jsonnet
{
  ALL: {
    type: 'install',
    deps: [
      @':README.md',        // local reference (same as 'README.md')
      @'./bin:main',        // relative reference
      @'//doc:html',        // absolute reference
      @'ext//lib:shared',   // external reference
    ],
  }
}
```

Evaluation
----------

The evaluation follows a strict, functional, call-by-value evaluation
mechanism; the precise evaluation is as follows.

 - Atomic values (`null`, booleans, strings, numbers) evaluate to
   themselves.
 - For lists, each entry is evaluated in the order they occur in the
   list; the result of the evaluation is the list of the results.
 - For maps, each value (and each computed key) is evaluated; the
   result is the map of the results.
 - Calls of functions evaluate to the result of applying the function
   to its arguments. The function has to be either a local function,
   one of the built-in ones, or a special function available in the
   given context (e.g., `jst.action` within the expression defining a
   rule).

All evaluation happens in an "environment" which is a map from strings
to semantic values. The values of the configuration (the runtime
variables) are only known when a target is analysed; all values not
depending on them are already evaluated when the file is read. Some
arguments have to be *static*, i.e., they must not depend on runtime
variables.

### Built-in syntactical constructs

#### Special forms

##### Runtime variables: `jst.env(name, default=null)`

Like [`$'<var>'`](#runtime-variables), but if the variable is not set (or `null`), the argument
`default` is evaluated and its value is the result of the evaluation. The
variable `name` has to be a static string.

``` jsonnet
// returns the value of variable 'a' or null if not set
jst.env('a')    // identical to the variable-read syntax: $a and $'a'

// returns 'foo' if variable 'a' is not set
jst.env('a', default='foo')
```

##### Conjunction and disjunction: `jst.all(args)` and `jst.any(args)`

For conjunction, if the argument `args` is
syntactically a list, its entries are sequentially evaluated
until a logically false value is found; in that case, the result
is `false`, otherwise true. If the argument has a different
shape, it is evaluated and has to evaluate to a list. The result
is the conjunction of the logical values of the entries. In
particular, `jst.all([])` evaluates to `true`.

For disjunction, the evaluation mechanism is the same, but the
truth values and connective are taken dually. So, `jst.all` and
`jst.any` are logical conjunction and disjunction, respectively,
using short-cut evaluation if syntactically admissible (i.e., if
the argument is syntactically a list). The operators `&&` and `||`
are the binary versions of them.

``` jsonnet
jst.all(['foo', 0, true])   // false (evaluation stops after 0)
jst.any(['', 42, false])    // true (evaluation stops after 42)
```

##### Mapping

###### Mapping over lists: `jst.foreach(func, range)`

First the argument `range` is evaluated and has to evaluate to
a list. For each entry of this list, the function `func` (accepting
exactly one argument) is called with that value. The result is the
list of those evaluation results. The same can be achieved by
[list comprehension](#list-comprehension).

``` jsonnet
local prefix(path) = '/usr/' + path;
jst.foreach(prefix, ['foo', 'bar'])     // ['/usr/foo', '/usr/bar']
[prefix(p) for p in ['foo', 'bar']]     // the same as list comprehension
```

###### Mapping over maps

Maps are mapped by iterating over their keys, which are returned in
lexicographic order (according to native byte order) by
[`jst.keys`](#unary-functions).

``` jsonnet
local map = {foo: 'FOO', bar: 'BAR'};
[k + '=' + map[k] for k in jst.keys(map)]   // ['bar=BAR', 'foo=FOO']
```

##### Zipping

###### `jst.zip_with(func, range1, range2)`

The arguments `range1` and `range2` are evaluated and have
to evaluate to lists. For each pair of entries, one from each
list, in order, the function `func` (accepting exactly two
arguments) is called with the entry of the first and of the second
list. The result of the evaluation is the list of those values. If the
input lists are of different lengths, any entry with no correspondence
in the other list is ignored.

``` jsonnet
local concat(a, b) = a + '_' + b;
jst.zip_with(concat, ['foo', 'bar'], ['fox', 'bat'])  // ['foo_fox', 'bar_bat']
```

###### `jst.zip_map(range_key, range_val)`

The arguments `range_key` and `range_val` are evaluated and have
to evaluate to lists. The result is a map, from an entry in
`range_key` to the entry in `range_val` with the same index.
If the input lists are of different lengths, any entry with no
correspondence in the other list is ignored. It is equivalent but
more convenient and more performant to a `jst.zip_with` expression
generating a list of maps combined with a `jst.union`.

``` jsonnet
jst.zip_map(['foo', 'bar'], ['fox', 'bat'])     // {bar: 'bat', foo: 'fox'}
```

##### Folding: `jst.foldl(func, init, range)`

The argument `range` is evaluated and has to evaluate to a list.
Starting from the result of evaluating `init`
a new value is obtained for each entry of the range list by
calling the function `func` with the list entry as first and the old
value as second argument. The result is the last value obtained.

``` jsonnet
local concat(it, accum) = accum + ':' + it;
jst.foldl(concat, 'foo', ['bar', 'baz'])    // 'foo:bar:baz'
```

#### Regular functions

The regular functions are available as functions attached to the
implicitly defined object `jst`. First the arguments are evaluated;
arguments can be given positionally or by name, and parameters with
default values are optional. Finally the function is applied to those
arguments to obtain the final result.

##### Unary functions

 - `jst.not(expr)` Return the logical negation of the argument, i.e.,
   if the argument is logically false, return `true`, and `false`
   otherwise. Same as `!expr`.

   ``` jsonnet
   jst.not([])     // true
   ```

 - `jst.nub_right(list)` The argument has to be a list. It is an error
   if that list contains (directly or indirectly) a name. The
   result is the input list, except that for all duplicate
   values, all but the rightmost occurrence is removed.

   ``` jsonnet
   jst.nub_right(['foo', 'bar', 'foo'])    // ['bar', 'foo']
   ```

 - `jst.nub_left(list)` The argument has to be a list. It is an error
   if that list contains (directly or indirectly) a name. The
   result is the input list, except that for all duplicate
   values, all but the leftmost occurrence is removed.

   ``` jsonnet
   jst.nub_left(['foo', 'bar', 'foo'])     // ['foo', 'bar']
   ```

 - `jst.basename(path)` The argument has to be a string. This string is
   interpreted as a path, and the file name thereof is
   returned.

   ``` jsonnet
   jst.basename('foo/bar/baz')     // 'baz'
   ```

 - `jst.keys(map)` The argument has to be a map. The result is the
   list of keys of this map, in lexicographical order
   (according to native byte order).

   ``` jsonnet
   jst.keys({foo: 'FOO', bar: 'BAR'})      // ['bar', 'foo']
   ```

 - `jst.values(map)` The argument has to be a map. The result are the
   values of that map, ordered by the corresponding keys
   (lexicographically according to native byte order).

   ``` jsonnet
   jst.values({foo: 'FOO', bar: 'BAR'})    // ['BAR', 'FOO']
   ```

 - `jst.range(size)` The argument is interpreted as a non-negative
   integer as follows. Non-negative numbers are rounded to the
   nearest integer; strings have to be the decimal
   representation of a number; everything else is considered
   zero. The result is a list of the given length, consisting
   of the decimal representations of the first non-negative
   integers.

   ``` jsonnet
   jst.range(3)        // ['0', '1', '2']
   jst.range('3.4')    // ['0', '1', '2']
   jst.range(-3)       // []
   ```

 - `jst.enumerate(items)` The argument has to be a list. The result is a
   map containing one entry for each element of the list. The
   key is the decimal representation of the position in the
   list (starting from `0`), padded with leading zeros to
   length at least 10. The value is the element. The padding is
   chosen in such a way that iterating over the resulting map
   (which happens in lexicographic order of the keys) has the
   same iteration order as the list for all lists indexable by
   32-bit integers.

   ``` jsonnet
   jst.enumerate(['foo', 'bar'])   // {'0000000000': 'foo', '0000000001': 'bar'}
   ```

- `jst.reverse(list)` The argument has to be a list. The result is a new list
  with the entries in reverse order.

   ``` jsonnet
   jst.reverse(['foo', true, ['bar']])     // [['bar'], true, 'foo']
   ```

- `jst.length(list)` The argument has to be a list. The result is the length
  of the list.

   ``` jsonnet
   jst.length(['foo', 'bar', 'baz'])   // 3
   ```

 - `jst.flatten(lists)` The argument has to be a list of lists. The result is
   the concatenation of those lists.

   ``` jsonnet
   jst.flatten([['foo', 'bar'], ['baz', 'qux']])   // ['foo', 'bar', 'baz', 'qux']
   ```

 - `jst.sum(numbers)` The argument has to be a list of numbers. The result is
   their sum (where the sum of the empty list is, of course, the
   neutral element 0).

   ``` jsonnet
   jst.sum([4, 2])     // 6
   ```

 - `jst.prod(numbers)` The argument has to be a list of numbers. The result
   is their product (where the product of the empty list is, of
   course, the neutral element 1).

   ``` jsonnet
   jst.prod([4, 2])    // 8
   ```

 - `jst.join_cmd(args)` The argument has to be a list of strings. A
   single string is returned that quotes the original vector in
   a way understandable by a POSIX shell. As the command for an
   action is directly given by an argument vector, `jst.join_cmd`
   is typically only used for generated scripts.

   ``` jsonnet
   jst.join_cmd(['echo', 'foo', "'bar' baz"])  // "'echo' 'foo' ''\''bar'\'' baz'"
   ```

 - `jst.json_encode(data)` The result is a single string that is the
   canonical JSON encoding of the argument (with minimal white
   space); all atomic values that are not part of JSON (i.e.,
   the added atomic values to represent build-internal values)
   are serialized as `null`.

   ``` jsonnet
   jst.json_encode(['foo', {baz: 'baz'}])  // '["foo",{"baz":"baz"}]'
   ```

##### Unary functions with keyword arguments

 - `jst.change_ending(path, ending='')` The argument has to be a string,
   interpreted as path. The ending is replaced by the value of
   the keyword argument `ending` (a string, default `''`).

   ``` jsonnet
   jst.change_ending('src/main.c', ending='.o')    // 'src/main.o'
   ```

 - `jst.join(items, sep='')` The argument has to be a list of strings. The
   return value is the concatenation of those strings,
   separated by the specified `sep` (string,
   default `''`).

   ``` jsonnet
   jst.join(['foo', 'bar'], sep=':')   // 'foo:bar'
   ```

 - `jst.escape_chars(str, chars='', prefix='\\')` Prefix in the
   argument every character occurring in `chars` (a string, default `''`)
   by `prefix` (a string, default `'\\'`).

   ``` jsonnet
   jst.escape_chars('foobar', chars='fb', prefix=',')  // ',foo,bar'
   ```

 - `jst.union(maps, disjoint=false, msg=null)` The argument has to be a
   list of maps. The result is a map containing as keys the union of the
   keys of the maps in that list. For each key, the value is the value
   of that key in the last map in the list that contains that key. If the
   static argument `disjoint` is true, it is an error, if two (or more)
   maps contain the same key, but map it to different values; it is also
   an error if the argument is a name-containing value. In case of such a
   user error, the argument `msg` is evaluated and the result of that
   evaluation reported in the error message.

   ``` jsonnet
   jst.union([{a: 'x'}, {b: 'y'}])                            // {a: 'x', b: 'y'}
   jst.union([{a: 'x'}, {a: 'x'}], disjoint=true)             // {a: 'x'}
   jst.union([{a: 'x'}, {a: 'y'}], disjoint=true)             // fails
   jst.union([{a: 'x'}, {a: 'y'}], disjoint=true, msg='foo')  // fails with 'foo'
   ```

 - `jst.to_subdir(map, subdir='.', flat=false, msg=null)` The argument has
   to be a map (not necessarily
   of artifacts). The keys as well as the `subdir` (string,
   default `'.'`) argument are interpreted as paths and keys
   are replaced by the path concatenation of those two paths.
   If the optional argument `flat` (default `false`)
   evaluates to a true value, the keys are instead replaced by
   the path concatenation of the `subdir` argument and the
   base name of the old key. It is an error if conflicts occur
   in this way; in case of such a user error, the argument
   `msg` is also evaluated and the result of that evaluation
   reported in the error message. Note that conflicts can also
   occur in non-flat staging if two keys are different as
   strings, but name the same path (like `'foo.txt'` and
   `'./foo.txt'`), and are assigned different values. It also
   is an error if the values for keys in conflicting positions
   are name-containing.

   ``` jsonnet
   jst.to_subdir({'a/bar': 'bar'}, subdir='foo')              // {'foo/a/bar': 'bar'}
   jst.to_subdir({'a/bar': 'bar'}, subdir='foo', flat=true)   // {'foo/bar': 'bar'}
   ```

 - `jst.from_subdir(map, subdir='.')` The argument has to be a map (not
   necessarily of artifacts). The keys of this map, as well as the value
   of keyword argument `subdir` (string, default `'.'`) are interpreted as
   paths; only those key-value pairs of the argument map are kept
   where the key refers to an entry in the specified `subdir`,
   and for those the path relative to the subdir is taken as new
   key. Those paths relative to the subdir are taken in canonical
   form; it is an error if non-trivial conflicts arise that way,
   i.e., if two keys that are kept normalize to the same relative
   path while the respective values are different.

   ``` jsonnet
   jst.from_subdir({'a/foo': 'foo', 'b/bar': 'bar'}, subdir='a')   // {foo: 'foo'}
   ```

##### Binary functions

 - `jst.eq(lhs, rhs)` The result is `true` is the arguments are equal,
   `false` otherwise. It is an error if one of the arguments
   are name-containing values. Same as `lhs == rhs`.

   ``` jsonnet
   jst.eq(['foo'], ['foo'])    // true
   ```

##### Other functions

 - `jst.get(key, map, default=null)` The `key` argument has to evaluate to
   a string and the `map` argument has to evaluate to a map (a logically
   false value is taken as empty map).
   If that map contains the given key and the corresponding
   value is non-`null`, the value is returned. Otherwise the
   `default` argument (with default `null`) is evaluated and
   returned. For literal maps, the [map lookup](#map-lookup) syntax can
   be used as well.

   ``` jsonnet
   jst.get('a', {a: 'x', b: 'y'})              // 'x'
   jst.get('c', {a: 'x', b: 'y'}, default='z') // 'z'
   ```

 - `jst.at(index, list, default=null)` The `list` argument has to evaluate
   to a list. The
   `index` argument has to evaluate to either a number (which
   is then rounded to the nearest integer) or a string (which
   is interpreted as an integer). If the index obtained in this
   way is valid for the obtained list, the entry at that index
   is returned; negative indices count from the end of the list.
   Otherwise the `default` argument (with default `null`) is
   evaluated and returned. For literal lists, the
   [list lookup](#list-lookup) syntax can be used as well.

   ``` jsonnet
   jst.at(-1, ['x', 'y'])                  // 'y'
   jst.at(2, ['x', 'y'], default='z')      // 'z'
   ```

##### Source references

The following functions explicitly refer to sources of the local module
in a `TARGETS` file; their argument has to be a static string.

 - `jst.file(path)` Reference to the file `path` in the local module or
   any submodule.

 - `jst.symlink(path)` Reference to the symlink `path` in the local module
   or any submodule.

 - `jst.tree(path)` Reference to the directory `path` in the local module
   or any submodule, as a single tree.

 - `jst.glob(pattern)` Reference to all files of the local module (not any
   submodule) matching the `pattern`.

   ``` jsonnet
   {lib: {type: @'rules//CC:library', srcs: [jst.glob('*.c')]}}
   ```

#### Constructs related to reporting of user errors

Normally, if an error occurs during the evaluation the error is
reported together with a stack trace. This, however, might not be
the most informative way to present a problem to the user,
especially if the underlying problem is a proper user error, e.g.,
in rule usage (leaving out mandatory arguments, violating semantical
prerequisites, etc.). To allow proper error reporting, the following
functions are available. All of them have an argument
`msg` that is evaluated (only) in case of an error, with the result of
that evaluation included in the error message presented to the user.

 - `jst.fail(msg)` Evaluation of this function unconditionally fails.

   ``` jsonnet
   jst.fail('foo')     // fails with message 'foo'
   ```

 - `jst.context(value, msg)` This function is only there to provide
   additional information in case of an error. Otherwise it is the
   identity function (i.e., the result of the evaluation
   is the result of evaluating the argument `value`).

   ``` jsonnet
   jst.context(jst.fail('boom'), msg='While computing x')  // fails, reporting both
   ```

 - `jst.assert_non_empty(value, msg=null)` Evaluate the argument (given
   by the parameter `value`). If it evaluates to a non-empty string, map,
   or list, return the result of the evaluation. Otherwise fail.

   ``` jsonnet
   jst.assert_non_empty([], msg='No sources given')    // fails
   ```

 - `jst.union(maps, disjoint=true, msg=null)` and
   `jst.to_subdir(map, subdir='.', flat=false, msg=null)`, see
   [above](#unary-functions-with-keyword-arguments).

#### Rule functions

The expression defining a rule additionally has access to the functions
described in [user-defined rules](rules.md#rule-functions), e.g.,
`jst.field` and `jst.action`.

Troubleshooting
---------------

### Manual evaluation

For testing purposes, the Jstlang evaluation can be run manually with the
subcommand `jst eval`. As input, it accepts Jstlang code from a file path or
from stdin (via `-`):

``` sh
$ echo "'foo' + jst.env('BAR', default='')" > snippet.jst
$ jst eval snippet.jst
"foo"
```

### Evaluate runtime variables

Similar to `jst build`, runtime variables for evaluation are provided as a JSON
object. This object is read either from file via the option `-c,--config`, or
from inline JSON on the command line via the option `-D,--defines`:

``` sh
$ jst eval -D'{"BAR":"bar"}' snippet.jst
"foobar"
```

### Generate jst-build IR

Advanced users might want to investigate the generated jst-build IR (JSON),
which can be requested by specifying the flag `--ir`. The IR is an expression
of the [JSON expression language](expressions.md):

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

> Note: For `TARGETS` files, use the flag `--targets`, which already implies the
> `--ir` flag.

### Static type checks

Static type errors will be detected immediately and reported with a detailed
log message:

``` sh
$ echo "'foo' + 42 + 'bar'" > snippet.jst
$ jst eval snippet.jst
ERROR: Lowering failed with:
       snippet.jst:1:6-11: Unsupported types for binary operation. Found String and Number.

       1 |  'foo' + 42 + 'bar'
         |       ^^^^^^

ERROR: Parsing input file failed.
```
