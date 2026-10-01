# Structured Text

The ST backend edits IEC 61131-3 Structured Text: POUs, variable sections, methods,
structs and enums, with the file parsed so the colouring, the outline and the
completion lists all come from what the file declares.

![a .st file open, Variables above Body, with the splitter between them](images/editor-st-split.png)

## One POU, two panes

A POU is edited as two panes with a splitter between them:

- **Variables** — `VAR`, `VAR_INPUT`, `VAR_OUTPUT`, `VAR_IN_OUT`, `VAR_EXTERNAL`, `VAR_GLOBAL`, `VAR_TEMP`
- **Body** — the cyclic code

Where the splitter sits is remembered per workspace in `undoApp.Editor.ini`.

## Methods

A `FUNCTION_BLOCK` has method tabs beside the POU tab; `PROGRAM` and `FUNCTION` do
not, because they have no methods in ST. Tabs can be dragged to reorder them.

- **`+`** adds a method, asking for its name and return type rather than inventing one
- **Right-click a method tab** for **Delete Method**, with a confirmation
- **Add Method** in a file's context menu in the Workspace tree does the same thing

Saving or compiling serializes every tab of the POU, not only the one on screen. The
editors for a method are created the first time the tab is opened and kept after it.

## The toolbar

| Button | |
|---|---|
| **Save** | writes the whole POU, every method tab included |
| **Compile** | hands the project to st2cpp — see [output.md](output.md) |
| **Validate** | parses and analyses without generating |
| **Close** | closes the document |
| **Tooltips** | off by default; see [autocomplete.md](autocomplete.md) |

## Semantic colouring

Identifiers are coloured by what they resolve to — a declared variable, a function, a
type, a keyword — using the symbol table st2cpp builds, not a keyword list. A name
that resolves to nothing is left uncoloured, which is how a typo shows up before it
is compiled.

## The outline

The **ST Outline** panel shows the parsed structure: each POU, its variable sections
and the declarations in them, its methods, and the file's structs and enums. It is
empty until a valid file is open, and it says so.

## Keyboard

| Key | |
|---|---|
| `Ctrl+S` | save |
| `Ctrl+N` | new POU |
| `Ctrl+W` | close the document |
| `Ctrl+Click` | go to the declaration of the identifier under the cursor |

The rest are in [keyboard.md](keyboard.md).