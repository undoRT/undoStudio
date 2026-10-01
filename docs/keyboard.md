# Keyboard

Every shortcut in the IDE, and where it stops being one.

## Global

| Keys | |
|---|---|
| `Ctrl+R` | **File > Open Recent Projects** |
| `Ctrl+P` | **File > Open Recent Files** |

Both are held off while anything owns the keyboard — a code editor, the terminal —
because in there the same two keys mean something else entirely, and stealing them
would break the program the user is in.

## In the Editor panel

| Keys | |
|---|---|
| `Ctrl+S` | save the open document |
| `Ctrl+W` | close the document |

## In the ST document

| Keys | |
|---|---|
| `Ctrl+S` | save the whole POU, every method tab included |
| `Ctrl+N` | new POU |
| `Ctrl+W` | close the document |
| `Ctrl+Click` | go to the declaration of the identifier under the cursor |

`Ctrl+O` is swallowed without doing anything, so it is not listed as a shortcut.

## In a completion list

| Keys | |
|---|---|
| `↑` `↓` | move one row |
| `PageUp` `PageDown` | move a page |
| `Tab` | next; inside a call, open the parameter picker |
| `Enter` | accept |
| `Esc` | dismiss |

A list dismissed at one spot stays dismissed there: the text still reads `inst.`, so
recomputing from scratch would put the list straight back on the next frame and
`Esc` would look like it had done nothing at all. Typing shifts the cursor and
reopens it.

## In the terminal

| Keys | |
|---|---|
| `Ctrl` + wheel | resize the text |

The **A-** and **A+** buttons do the same thing and are the discoverable route.

## In a popup

| Keys | |
|---|---|
| `↑` `↓` `Enter` | walk a recent list and open the one chosen |
| `Esc`, or a click elsewhere | close |

`Ctrl+R` and `Ctrl+P` open those popups and hand them the keyboard, so the list can
be walked without a mouse.