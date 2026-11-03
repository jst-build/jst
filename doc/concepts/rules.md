User-defined Rules
==================

Targets are defined in terms of high-level concepts like "libraries",
"binaries", etc. In order to translate these high-level definitions
into actionable tasks, the user defines rules, explaining at a single
point how all targets of a given type are built.

Rules files
-----------

Rules are defined in rules files (by default named `RULES`), written in
[Jstlang](jstlang.md). A rules file evaluates to a map from rule names
to their rule definition. For rules, the same naming scheme as for
targets applies. However, built-in rules (always named by a single
string) take precedence in naming; to explicitly refer to a rule defined
in the current module, the module has to be specified, possibly by a
relative path, e.g., `@'./:install'`.

Code shared between rules is written as functions. Functions used by a
single rules file are defined as `local` functions before the map of
rules; functions used by several rules files are defined in separate
Jstlang files and imported, either from the same rule root
(`import '/CC/expressions.jst'`) or from the rule root of another
repository (`import @'rules//CC/expressions.jst'`).

``` jsonnet
// Stage all artifacts of the targets in field "srcs"
local srcs() = jst.union(
  [jst.dep_artifacts(dep) for dep in jst.field('srcs')],
  disjoint=true,
);

{
  copy: {
    target_fields: ['srcs'],
    expression: jst.result(artifacts=srcs()),
  },
}
```

Basic components of a rule
--------------------------

A rule is defined through a map with various keys. The only mandatory
key is `expression` containing the defining expression of the rule.

The rule definition has to be a literal map, as have to be the maps
among its values (e.g., `config_transitions`). The values of
`expression` and of the entries of `config_transitions` are evaluated
for every target defined by that rule, in the target's configuration.
All other values are evaluated once, when the rules file is read;
therefore, they must not depend on the configuration.

### `bool_fields`, `string_fields`, `target_fields`, `config_bool_fields`, and `config_string_fields`

These keys specify the fields that a target defined by that rule can
have. In particular, those have to be disjoint lists of strings.

A bool field can be set to any value, which is reduced to its truth
value; so the rule obtains `true` or `false` via `jst.field`, with
`false` if the field is not set. A string field has to evaluate to a
list of strings, whereas `target_fields` have to evaluate to a list of
target references. Those references are evaluated immediately, and in
the name context of the target they occur in.

The config fields (`config_bool_fields` and `config_string_fields`) are
evaluated like bool and string fields, respectively, but before the
target fields; hence they can be used by the rule to specify config
transitions for the target fields. `bool_fields` and `string_fields` on
the other hand are evaluated *after* the target fields; hence the rule
cannot use them to specify a configuration transition, however the
target definition in those fields may use the `outs` and `runfiles`
functions to have access to the names of the artifacts or runfiles of a
target specified in one of the target fields.

### `implicit`

This key specifies a map of implicit dependencies. The keys of the map
are additional target fields, the values are the fixed list of targets
for those fields. Target references are interpreted relative to the
repository and module the rule is defined in, not the one the rule is
used in. Other than this, those fields are evaluated the same way as
target fields settable on invocation of the rule.

### `config_vars`

This is a list of strings specifying which parts of the configuration
the rule uses. The defining expression of the rule is evaluated in a
configuration restricted to those variables; they can be read via
`$'<var>'` or `jst.env('<var>', default=...)`. If one of those variables
is not specified in the configuration, its value is `null` (or the given
default value).

### `config_transitions`

This key specifies a map of (some of) the target fields (whether
declared as `target_fields` or as `implicit`) to a list of
configuration transitions. Each transition is a map that amends the
current configuration on the domain of that map to the given value. The
transitions have access to the `config_vars` and the config fields
(via `jst.field`).

### `expression`

This specifies the defining expression of the rule, which has to
evaluate to a result value (created by `jst.result`). Besides the
[standard library](jstlang.md#regular-functions), it has access to the
[rule functions](#rule-functions) to read the fields of the
target (`jst.field`), to access the targets depended upon
(`jst.dep_artifacts`, `jst.dep_runfiles`, and `jst.dep_provides`), and
to define new artifacts (e.g., `jst.make_blob`, `jst.make_symlink`, and
`jst.action`). The rule functions can also be used in functions defined
outside the rule definition (locally in the rules file, or in imported
files), as long as those functions are called from the defining
expression of a rule.

Target fields evaluate to abstract names of the targets. These names are
opaque within Jstlang (but meaningful when reported in error messages)
and should only be used to be passed on to the rule functions that
expect names as inputs. When accessing a target depended upon, a
configuration transition can be given; it is an error to request a
target-transition pair for a target that was not requested in the given
transition through one of the target fields.

Rule functions
--------------

The following functions are only available in the defining expression of
a rule and in the `config_transitions` of a rule, including functions
called from there. They are evaluated for every target defined by the
rule. Abstract names of targets are obtained from `jst.field` only.

 - `jst.field(name)` The value of the field `name` of the target. For
   string and config fields, this is a list of strings; for target
   fields, it is the list of the abstract names of the targets, which
   can only be passed on to the other rule functions.

   ``` jsonnet
   jst.field('name')   // e.g., ['hello']
   ```

 - `jst.dep_artifacts(dep, transition={})` The artifacts of the target
   `dep` depended upon, staged at their logical paths, as a map. The
   target has to be requested in the given configuration `transition`
   through one of the target fields.

   ``` jsonnet
   jst.union([jst.dep_artifacts(dep) for dep in jst.field('srcs')])
   ```

 - `jst.dep_runfiles(dep, transition={})` The runfiles of the target
   `dep` depended upon, staged at their logical paths, as a map.

   ``` jsonnet
   jst.union([jst.dep_runfiles(dep) for dep in jst.field('deps')])
   ```

 - `jst.dep_provides(dep, provider, transition={}, default=[])` The
   value of the key `provider` in the provides map of the target `dep`
   depended upon. If the key is missing or its value is `null`, the
   `default` is returned.

   ``` jsonnet
   [jst.dep_provides(dep, 'packages', default={}) for dep in jst.field('deps')]
   ```

 - `jst.make_blob(data='')` A (non-executable) file artifact with the given
   content `data`.

   ``` jsonnet
   {'hello.txt': jst.make_blob('Hello World!\n')}
   ```

 - `jst.make_symlink(target)` A symlink artifact pointing to the path
   `target`. Like any symlink, the target is interpreted relative to the
   directory the symlink is staged in, and it does not need to exist (the
   symlink is not resolved). The `target` has to be a relative path that
   does not point upwards out of that directory (e.g., `'lib.so.1'` or
   `'sub/file'`, but not `'/usr/lib'` or `'../file'`). Not to be confused
   with `jst.symlink(path)`, which refers to an existing symlink in the
   source tree.

   ``` jsonnet
   {'libfoo.so.1': jst.make_symlink('libfoo.so.1.2.3')}
   ```

 - `jst.make_tree(stage)` A tree artifact created from the map `stage`
   from paths to artifacts. It is an error if the map cannot be
   transformed into a tree (e.g., due to staging conflicts).

   ``` jsonnet
   {docs: jst.make_tree({'README.md': jst.make_blob('# Docs\n')})}
   ```

 - `jst.tree_overlay(stages, disjoint=false)` A tree artifact as
   overlay of the trees of the given list `stages` of maps of artifacts,
   in the given order (latest wins on each path). If the static argument
   `disjoint` is true, it is an error if the trees conflict on any path.
   The overlay is computed in the build phase, see
   [tree overlays](tree-overlay.md).

   ``` jsonnet
   jst.tree_overlay([jst.dep_artifacts(dep) for dep in jst.field('deps')])
   ```

 - `jst.action(inputs={}, cmd, outs=[], out_dirs=[], env={}, cwd='',
   label=null, may_fail=[], fail_message='action failed', no_cache=[],
   timeout_scaling=1.0, execution_properties={})` Define an action
   creating new artifacts by running the command `cmd` (a non-empty list
   of strings) in the directory `cwd` (relative to the action root), in
   the environment `env`, on the map `inputs` from paths to artifacts.
   The result is the map of the outputs, staged at the paths given in
   `outs` (output files) and `out_dirs` (output directories); those
   paths are relative to the action root, not to `cwd`, and have to be
   disjoint. It is an error if the command does not create all of them.
   The command must not modify its inputs, and must not make any
   assumption about the location of the working directory in the file
   system. The keys of `inputs` are interpreted as paths; keys naming
   the same path (e.g., `'foo.txt'` and `'./foo.txt'`) must have equal
   values. The `label` is a short description of the action for the
   [progress reporting](progress-reporting.md), which crops labels
   longer than 40 characters. For the static lists `may_fail` and
   `no_cache` (taintedness strings) as well as `fail_message`,
   `timeout_scaling`, and `execution_properties`, see
   [cache pragma](cache-pragma.md) and
   [execution properties](execution-properties.md).

   ``` jsonnet
   jst.action(
     inputs={'in.txt': jst.make_blob('hello\n')},
     cmd=['/bin/sh', '-c', 'tr a-z A-Z < in.txt > out.txt'],
     outs=['out.txt'],
     label='Converting in.txt',
   )['out.txt']
   ```

 - `jst.result(artifacts={}, runfiles={}, provides={})` The result of
   the target, consisting of the maps `artifacts` and `runfiles` from
   paths to artifacts, and the map `provides` with information for
   consuming targets. The defining expression of a rule has to evaluate
   to a result. The keys of `artifacts` and `runfiles` are interpreted
   as paths; it is an error if this yields conflicts. Other targets see
   them as normalized paths. Results are opaque and cannot be
   deconstructed.

   ``` jsonnet
   jst.result(artifacts={'hello.txt': jst.make_blob('Hello World!\n')})
   ```

 - `jst.value_node(result)` A graph node of a target with the fixed
   `result`, to be used for [anonymous targets](anonymous-targets.md).

   ``` jsonnet
   jst.value_node(jst.result(artifacts={'a.proto': jst.make_blob('...')}))
   ```

 - `jst.abstract_node(node_type, bool_fields={}, string_fields={},
   target_fields={})` An internal graph node, to be used for [anonymous
   targets](anonymous-targets.md). The `node_type` is the key into the rule map
   of the anonymous target; `bool_fields` maps field name to values (reduced to
   bools), `string_fields` maps field names to lists of strings, and
   `target_fields` maps field names to lists of nodes. The keys of
   `bool_fields`, `string_fields`, and `target_fields` have to be disjoint.

   ``` jsonnet
   jst.abstract_node(
     'library',
     string_fields={name: ['addressbook']},
     target_fields={srcs: srcs_nodes},
   )
   ```

Evaluation of a target
----------------------

A target defined by a user-defined rule is evaluated in the following
way.

 - First, the config fields are evaluated.

 - Then, the target-fields are evaluated. This happens for each field
   as follows.

    - The configuration transition for this field is evaluated and the
      transitioned configurations determined.
    - The argument expression for this field is evaluated. The result
      is interpreted as a list of target names. Each of those targets
      is analyzed in all the specified configurations.

 - The string fields are evaluated. If the expression for a string
   field queries a target (via `outs` or `runfiles`), the value for
   that target is returned in the first configuration. The rational
   here is that such generator expressions are intended to refer to the
   corresponding target in its "main" configuration; they are hardly
   used anyway for fields branching their targets over many
   configurations.

 - The effective configuration for the target is determined. The target
   effectively has used of the configuration the variables used by the
   `arguments_config` in the rule invocation, the `config_vars` the
   rule specified, and the parts of the configuration used by a target
   dependent upon. For a target dependent upon, all parts it used of
   its configuration are relevant expect for those fixed by the
   configuration transition.

 - The rule expression is evaluated and the result of that evaluation
   is the result of the rule.

Example of developing a rule
----------------------------

Let's consider step by step an example of writing a rule. Say we want
to write a rule that programmatically patches some files.

### Framework: The minimal rule

Every rule has to have a defining expression evaluating to a result
value. So the minimally correct rule is the `null` rule in the following
example rules file.

``` jsonnet
{
  'null': {expression: jst.result()},
}
```

This rule accepts no parameters, and has the empty map as artifacts,
runfiles, and provided data. So it is not very useful.

### String inputs

Let's allow the target definition to have some fields. The most simple
fields are `string_fields`; they are given by a list of strings. In the
defining expression we can access them directly via `jst.field`.
Strings can be used when defining maps, but we can also create
artifacts from them, using `jst.make_blob`. We define values step by step,
using `local` definitions.

``` jsonnet
{
  script_only: {
    string_fields: ['script'],
    expression:
      local script_content = jst.join(
        ['H'] + jst.field('script') + ['w', 'q', ''],
        sep='\n',
      );
      local script = {'script.ed': jst.make_blob(script_content)};
      jst.result(artifacts=script),
  },
}
```

### Target inputs and derived artifacts

Now it is time to add the input files. Source files are targets like any
other target (and happen to contain precisely one artifact). So we add a
target field `srcs` for the file to be patched. Here we have to keep
in mind that, on the one hand, target fields accept a list of targets
and, on the other hand, the artifacts of a target are a whole map. We
chose to patch all the artifacts of all given `srcs` targets. We can
iterate over lists with list comprehensions and over maps with map
comprehensions over their keys.

Next, we have to keep in mind that targets may place their artifacts at
arbitrary logical locations. For us that means that first we have to
make a decision at which logical locations we want to place the output
artifacts. As one thinks of patching as an in-place operation, we chose
to logically place the outputs where the inputs have been. Of course, we
do not modify the input files in any way; after all, we have to define a
mathematical function computing the output artifacts, not a collection
of side effects. With that choice of logical artifact placement, we have
to decide what to do if two (or more) input targets place their
artifacts at logically the same location. We could simply take a
"latest wins" semantics (keep in mind that target fields give a list
of targets, not a set) as provided by `jst.union`. We chose to consider
it a user error if targets with conflicting artifacts are specified.
This is provided by `jst.union` with `disjoint=true`, which also allows
to specify an error message to be provided the user. Here, conflict
means that values for the same map position are defined in a different
way.

The actual patching is done by `jst.action`. We have the script already;
to make things easy, we stage the input to a fixed place and also expect
a fixed output location, which we look up in the map of outputs
returned by `jst.action`. Then the actual command is a simple shell
script. The only thing we have to keep in mind is that we want useful
output precisely if the action fails. Also note that, while we define
our actions in a comprehension, they will be executed in parallel, as
none of them depends on the output of another one of them.

``` jsonnet
{
  ed_patch: {
    string_fields: ['script'],
    target_fields: ['srcs'],
    expression:
      local script_content = jst.join(
        ['H'] + jst.field('script') + ['w', 'q', ''],
        sep='\n',
      );
      local script = {'script.ed': jst.make_blob(script_content)};
      local patched_files_per_target = [
        local files = jst.dep_artifacts(src);
        {
          [file_name]: jst.action(
            inputs=script + {'in': files[file_name]},
            cmd=[
              '/bin/sh',
              '-c',
              'cp in out && chmod 644 out && /bin/ed out < script.ed > log 2>&1 || (cat log && exit 1)',
            ],
            outs=['out'],
          ).out
          for file_name in jst.keys(files)
        }
        for src in jst.field('srcs')
      ];
      local artifacts = jst.union(
        patched_files_per_target,
        disjoint=true,
        msg='srcs artifacts must not overlap',
      );
      jst.result(artifacts=artifacts),
  },
}
```

A typical invocation of that rule would be a target file like the
following.

``` jsonnet
{
  'input.txt': {
    type: 'ed_patch',
    script: ['%g/world/s//user/g', '%g/World/s//USER/g'],
    srcs: [jst.file('input.txt')],
  },
}
```

As the input file has the same name as a target (in the same module), we
use the explicit file reference in the specification of the sources.

### Implicit dependencies and config transitions

Say, instead of patching a file, we want to generate source files from
some high-level description using our actively developed code generator.
Then we have to do some additional considerations.

 - First of all, every target defined by this rule not only depends on
   the targets the user specifies. Additionally, our code generator is
   also an implicit dependency. And as it is under active development,
   we certainly do not want it to be taken from the ambient build
   environment (as we did in the previous example with `ed` which,
   however, is a pretty stable tool). So we use an `implicit` target
   for this.
 - Next, we notice that our code generator is used during the build. In
   particular, we want that tool (written in some compiled language) to
   be built for the platform we run our actions on, not the target
   platform we build our final binaries for. Therefore, we have to use
   a configuration transition.
 - As our defining expression also needs the configuration transition
   to access the artifacts of that implicit target, we better define it
   as a reusable function. Other rules in our rule collection might also
   have the same task; so `/transitions/expressions.jst` might be a good
   place to define it. In fact, it can look like the function `for_host`
   in our own code base.

So, the overall organization of our rule might be as follows.

``` jsonnet
local transitions = import '/transitions/expressions.jst';

{
  generated_code: {
    target_fields: ['srcs'],
    implicit: {generator: [@'//generators:foogen']},
    config_vars: ['ARCH', 'HOST_ARCH', 'TARGET_ARCH'],
    config_transitions: {generator: [transitions.for_host()]},
    expression: ...,
  },
}
```

### Providing information to consuming targets

In the simple case of patching, the resulting file is indeed the only
information the consumer of that target needs; in fact, the main point
was that the resulting target could be a drop-in replacement of a source
file. A typical rule, however, defines something like a library and a
library is much more than just the actual library file and the public
headers: a library may depend on other libraries; therefore, in order to
use it, we need

 - to have the header files of dependencies available that might be
   included by the public header files of that library,
 - to have the libraries transitively depended upon available during
   linking, and
 - to know the order in which to link the dependencies (as they might
   have dependencies among each other).

In order to keep a maintainable build description, all this should be
taken care of by simply depending on that library. We do
*not* want the consumer of a target having to be aware of
such transitive dependencies (e.g., when constructing the link command
line), as it used to be the case in early build tools like `make`.

It is a deliberate design choice that a target is given only by the
result of its analysis, regardless of where it is coming from.
Therefore, all this information needs to be part of the result of a
target. Such kind of information is precisely, what the mentioned
`provides` map is for. As a map, it can contain an arbitrary amount of
information and the interface function `jst.dep_provides` is in such a
way that adding more providers does not affect targets not aware of them
(there is no function asking for all providers of a target). The keys
and their meaning have to be agreed upon by a target and its consumers.
As the latter, however, typically are a target of the same family
(authored by the same group), this usually is not a problem.

A typical example of computing a provided value is the `link-args` in
the rules used by `jst` itself. They are defined by the following
expression.

``` jsonnet
jst.nub_right(
  jst.flatten([
    jst.keys(lib),
    link_args_deps(deps_fieldnames=deps_fieldnames),
    link_external,
  ]),
)
```

This expression

 - collects the respective provider of its dependencies,
 - adds itself in front, and
 - deduplicates the resulting list, keeping only the right-most
   occurrence of each entry.

In this way, the invariant is kept, that the `link-args` form a
topological ordering of the dependencies (in the order that each entry
is mentioned before its dependencies).
