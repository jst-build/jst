# Justlang: Standard Library

## Introduction

The entire standard library is available as functions attached to the implicitly
defined object `jst`. Some library functions may require literal argument types,
which means that their expression tree may not contain any runtime variables.
Parameters with default values are optional and may be omitted.

## Available functions

### `jst.env(name, default=null)`

Access the value of a runtime variable (the target is configured with).

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `name`    | *LiteralString* | Name of the variable to read        | None |
| `default` | *Any*           | Return value if variable is not set | `null` |

**Returns:** The value of the runtime variable as type *Any*.

**Example:**
``` jsonnet
// returns the value of variable 'a' or null if not set
jst.env('a')

// returns 'foo' if variable 'a' is not set
jst.env('a', default='foo')
```

---

### `jst.at(index, list, default=null)`

Get value at index from list.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `index`   | *Number*\|*String* | Index to lookup from list | None |
| `list`    | *List*             | List to get value from | None |
| `default` | *Any*              | Return value if index is invalid | `null` |

> Note: *String*-type arguments will be converted to *Number* or considered `0`
> if the conversion fails. *Number*-type arguments will be rounded to nearest
> integer and negative numbers count from the end of the list. For literal
> lists, you can also use the [list lookup syntax](./getting-started.md#list-lookup).

**Returns:** The value at the index as type *Any*.

**Example:**
``` jsonnet
// returns 'x'
jst.at('0', ['x', 'y'])

// returns 'y'
jst.at(-1, ['x', 'y'])

// returns 'z'
jst.at(2, ['x', 'y'], default='z')
```

---

### `jst.get(key, map, default=null)`

Get value of key from map.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `key`     | *String* | Key to lookup from map | None |
| `map`     | *Map*    | Map to get value from | None |
| `default` | *Any*    | Return value if key is not found | `null` |

> Note: `map` will be treated as empty map for non-positive [*truth values*](./getting-started.md#truth-value).
> For literal maps, you can also use the [map lookup syntax](./getting-started.md#map-lookup).

**Returns:** The value of the key as type *Any*.

**Example:**
``` jsonnet
// returns 'x'
jst.get('a', {a:'x', b:'y'})

// returns 'z'
jst.get('c', {a:'x', b:'y'}, default='z')

// returns 'z'
jst.get('c', null, default='z')
```

---

### `jst.join(items, sep='')`

Concatenate strings from the list.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `items`  | *List*   | List of strings to concatenate | None |
| `sep`    | *String* | Separator used for concatenation | `''` |

**Returns:** The concatenated string as type *String*.

**Example:**
``` jsonnet
// returns 'foobar'
jst.join(['foo', 'bar'])

// returns 'foo:bar'
jst.join(['foo', 'bar'], sep=':')
```

---

### `jst.flatten(lists)`

Concatenate lists to single list.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `lists`  | *List*   | List of lists to concatenate | None |

**Returns:** The concatenated list as type *List*.

**Example:**
``` jsonnet
// returns ['foo', 'bar', 'baz', 'qux']
jst.flatten([['foo', 'bar'], ['baz', 'qux']])
```

---

### `jst.union(maps, disjoint=false)`

Compute the union of given maps.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `maps`      | *List*        | List of maps to combine | None |
| `disjoint`  | *LiteralBool* | Require disjoint maps   | `false` |

> Note: disjoint allows non-conflicting values for the same key.

**Returns:** The unified map as type *Map*.

**Example:**
``` jsonnet
// returns {a: 'x', 'b': 'y'}
jst.union([{a: 'x'}, {b: 'y'}])

// returns {a: 'x'} (non-conflicting value)
jst.union([{a: 'x'}, {a: 'x'}], disjoint=true)

// fails due to conflicting values for key a
jst.union([{a: 'x'}, {a: 'y'}], disjoint=true)
```

---

### `jst.sum(numbers)`

Compute the sum of given numbers.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `numbers` | *List* | List of numbers | None |

> Note: the empty list will return the neutral element `0`.

**Returns:** The sum as type *Number*.

**Example:**
``` jsonnet
// returns 0
jst.sum([])

// returns 6
jst.sum([4, 2])
```

---

### `jst.prod(numbers)`

Compute the product of given numbers.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `numbers` | *List* | List of numbers | None |

> Note: the empty list will return the neutral element `1`.

**Returns:** The product as type *Number*.

**Example:**
``` jsonnet
// returns 1
jst.prod([])

// returns 8
jst.prod([4, 2])
```

---

### `jst.all(args)`

Compute the conjunction of given arguments' [*truth value*](./getting-started.md#truth-value).

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `args` | *List* | List of *Any*-type arguments  | None |

> Note: short-circuit evaluation is applied for literal lists.

**Returns:** The conjunction as type *Bool*.

**Example:**
``` jsonnet
// returns true
jst.all([])

// returns true
jst.all(['foo', 42])

// returns false (evaluation stops after 0)
jst.all(['foo', 0, true])
```

---

### `jst.any(args)`

Compute the disjunction of given arguments' [*truth value*](./getting-started.md#truth-value).

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `args` | *List* | List of *Any*-type arguments  | None |

> Note: short-circuit evaluation is applied for literal lists.

**Returns:** The disjunction as type *Bool*.

**Example:**
``` jsonnet
// returns false
jst.any([])

// returns false
jst.any(['', 0])

// returns true (evaluation stops after 42)
jst.any(['', 42, false])
```

---

### `jst.keys(map)`

Get the keys of a map.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `map` | *Map* | The map to get keys from | None |

**Returns:** The keys as type *List*.

**Example:**
``` jsonnet
// returns []
jst.keys({})

// returns ['foo', 'bar']
jst.keys({foo: 'foo', bar: 'bar'})
```

---

### `jst.range(size)`

Generate range of numbers.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `size` | *Any* | The size of the range | None |

> Note: *String*-type arguments will be converted to *Number* or considered `0`
> if the conversion fails. *Number*-type arguments will be rounded to nearest
> integer or considered `0` if negative. Everything else is considered `0`.

**Returns:** The numbers in string representation as type *List*.

**Example:**
``` jsonnet
// returns ['0', '1', '2']
jst.range(3)

// returns ['0', '1', '2']
jst.range('3.4')

// returns []
jst.range(-3)

// returns []
jst.range('foo')
```

---

### `jst.reverse(list)`

Reverse item's position of the list.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `list` | *List* | The list to reverse | None |

**Returns:** The reversed items as type *List*.

**Example:**
``` jsonnet
// returns [['bar'], true, 'foo']
jst.reverse(['foo', true, ['bar']])
```

---

### `jst.length(list)`

Get the length of the list.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `list` | *List* | The list to get length from | None |

**Returns:** The list length as type *Number*.

**Example:**
``` jsonnet
// returns 0
jst.length([])

// returns 3
jst.length(['foo', 'bar', 'baz'])
```

---

### `jst.nub_left(list)`

Remove duplicates from list, except the left-most ones.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `list` | *List* | The list to deduplicate | None |

**Returns:** The unique entries as type *List*.

**Example:**
``` jsonnet
// returns ['foo', 'bar']
jst.nub_left(['foo', 'bar', 'foo'])
```

---

### `jst.nub_right(list)`

Remove duplicates from list, except the right-most ones.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `list` | *List* | The list to deduplicate | None |

**Returns:** The unique entries as type *List*.

**Example:**
``` jsonnet
// returns ['bar', 'foo']
jst.nub_right(['foo', 'bar', 'foo'])
```

---

### `jst.enumerate(list)`

Generate enumeration object from list values, with keys being the decimal
representation of the list position, padded with leading zeros to length 10.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `list` | *List* | The list to enumerate | None |

**Returns:** The enumeration object as type *Map*.

**Example:**
``` jsonnet
// returns {}
jst.enumerate([])

// returns {'0000000000': 'foo', '0000000001': 'bar', '0000000002': 'baz'}
jst.enumerate(['foo', 'bar', 'baz'])
```

---

### `jst.foreach(func, range)`

Run function for each element in range.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `func`  | *Func* | The function to call | None |
| `range` | *List* | The iteration range  | None |

> Note: `func` must be a function accepting exactly one argument.

**Returns:** The resulting elements as type *List*.

**Example:**
``` jsonnet
local prefix(path) = '/usr/' + path;

// returns ['/usr/foo', '/usr/bar']
jst.foreach(prefix, ['foo', 'bar'])

// identical to this list comprehension
[prefix(p) for p in ['foo', 'bar']]
```

---

### `jst.foldl(func, init, range)`

Run function to fold elements in range from left (to right).

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `func`  | *Func* | The function to call   | None |
| `init`  | *Any*  | The initial fold value | None |
| `range` | *List* | The iteration range    | None |

> Note: `func` must be a function accepting exactly one two arguments, the
> iteration variable first and the accumulation variable second.

**Returns:** The resulting value as return type of `func`.

**Example:**
``` jsonnet
local concat(it, accum) = accum + ':' + it;

// returns 'foo:bar:baz'
jst.foldl(concat, 'foo', ['bar', 'baz'])
```

---

### `jst.basename(path)`

Get basename from path.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `path`  | *String* | The path to get the basename from | None |

**Returns:** The basename as type *String*.

**Example:**
``` jsonnet
// returns 'baz'
jst.basename('foo/bar/baz')
```

---

### `jst.to_subdir(map, subdir='.', flat=false, msg='')`

Prepend map's keys by subdir.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `map`    | *Map*           | The map to prepend keys from | None |
| `subdir` | *String*        | The path to prepend          | `'.'` |
| `flat`   | *Bool*          | Replace dirname by subdir    | `false` |
| `msg`    | *LiteralString* | Error message for conflicts  | `''` |

> Note: Resulting paths (keys) will be normalized, therefore conflicts may also
> occur with `flat=false`. Non-conflicting values for the same key are allowed.

**Returns:** The modified object as type *Map*.

**Example:**
``` jsonnet
// returns {'foo/a/bar': 'bar', 'foo/b/baz': 'baz'}
jst.to_subdir({'a/bar': 'bar', 'b/baz': 'baz'}, subdir='foo')

// returns {'foo/bar': 'bar', 'foo/baz': 'baz'}
jst.to_subdir({'a/bar': 'bar', 'b/baz': 'baz'}, subdir='foo', flat=true)

// returns {'foo/bar': 'bar'} (non-conflicting value)
jst.to_subdir({'a/bar': 'bar', 'b/bar': 'bar'}, subdir='foo', flat=true)

// fails due to conflict at 'foo/bar' after normalization
jst.to_subdir({'bar': 'bar', './bar': 'baz'}, subdir='foo')

// fails due to conflict at 'foo/bar' after flattening and normalization
jst.to_subdir({'a/bar': 'bar', 'b/bar': 'baz'}, subdir='foo', flat=true)
```

---

### `jst.from_subdir(map, subdir)`

Strip subdir from map's keys.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `map`    | *Map*    | The map to strip keys from | None |
| `subdir` | *String* | The path to strip          | `'.'` |

> Note: Resulting paths (keys) will be normalized, therefore conflicts may occur.
> Non-conflicting values for the same key are allowed. Keys not matching the
> subdir path will be omitted from the output object.

**Returns:** The modified object as type *Map*.

**Example:**
``` jsonnet
// returns {'foo': 'foo', 'bar': 'bar'}
jst.from_subdir({'a/foo': 'foo', 'a/bar': 'bar'}, subdir='a')

// returns {'foo': 'foo'} (non-conflicting value)
jst.from_subdir({'a/foo': 'foo', 'a/./foo': 'foo'}, subdir='a')

// fails due to conflict at 'foo' after normalization
jst.from_subdir({'a/foo': 'foo', 'a/./foo': 'bar'}, subdir='a')
```

---

### `jst.join_cmd(args)`

Join arguments to shell-quoted command line.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `args` | *List* | The list of arguments to join | None |

**Returns:** The command line as type *String*.

**Example:**
``` jsonnet
// returns "'test' '-n' ''"
jst.join_cmd(['test', '-n', ''])

// returns "'echo' 'foo' ''\''bar'\'' baz'"
jst.join_cmd(['echo', 'foo', "'bar' baz"])
```

---

### `jst.json_encode(data)`

JSON encode data.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `data` | *Any* | The data to JSON encode | None |

**Returns:** The JSON representation of `data` as type *String*.

**Example:**
``` jsonnet
// returns '["foo", ["bar"], {"baz": "baz"}]'
jst.json_encode(['foo', ['bar'], {baz: 'baz'}])
```

---

### `jst.change_ending(path, ending)`

Change the extension of a filename component.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `path` | *String* | The file path to change | None |
| `ending` | *String* | The extension to insert | None |

**Returns:** The changed path as type *String*.

**Example:**
``` jsonnet
// returns 'src/main.o'
jst.change_ending('src/main.c', '.o')

// returns 'foo'
jst.change_ending('foo.bar', '')
```

---

### `jst.escape_chars(str, chars, prefix='\\')`

Escape characters from string.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `str`    | *String* | The string to process    | None |
| `chars`  | *String* | The characters to escape | None |
| `prefix` | *String* | The escape character     | None |

**Returns:** The escaped string as type *String*.

**Example:**
``` jsonnet
// returns ',foo,bar'
jst.escape_chars('foobar', 'fb', ',')
```

---

### `jst.fail(msg)`

Fail the evaluation with given error message.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `msg` | *Any* | The error message | None |

> Note: *Any*-type `msg` will be serialized to JSON before printing.

**Returns:** *Nothing*

**Example:**
``` jsonnet
// fails at runtime with message 'foo'
jst.fail('foo')
```

---

### `jst.file(path)`

Explicit local file reference.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `path` | *LiteralString* | Path of the file | None |

> Note: `path` must point to a file from the local module or any submodule.

**Returns:** *Reference*

---

### `jst.symlink(path)`

Explicit local symlink reference.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `path` | *LiteralString* | Path of the symlink | None |

> Note: `path` must point to a symlink from the local module or any submodule.

**Returns:** *Reference*

---

### `jst.tree(path)`

Explicit local tree reference.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `path` | *LiteralString* | Path of the tree | None |

> Note: `path` must point to a tree from the local module or any submodule.

**Returns:** *Reference*

---

### `jst.glob(pattern)`

Explicit reference for collecting local files by pattern.

| Parameter | Type | Description | Default value |
|-|:-|:-|:-:|
| `pattern` | *LiteralString* | Pattern to match files | None |

> Note: `pattern` is only applied to the local module, **not** any submodule.

**Returns:** *Reference*

---
