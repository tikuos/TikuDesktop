# Markdown pages

The Help reader accepts **HTML and Markdown** pages in the help folder.
Markdown uses the same Find, Search all help, Outline, bookmarks, and copying
controls as HTML.

## Formatting

Use headings, **bold**, *emphasis*, and `inline code` to make a page readable.

- Bulleted lists collect related items.
  - Indented lists group details.
- Numbered lists describe a sequence.

1. Open a help page.
2. Find the section you need.
3. Copy an example without running it.

> Reading help never runs code or changes a connected device.

## Code examples

Fenced code preserves spaces, line breaks, and literal characters. The language
label is accepted, but syntax highlighting is not currently provided.

```basic
PRINT "Hello from TikuBasic"
END
```

## Tables

| Control | Purpose |
| --- | --- |
| Find | Search this page |
| Outline | Jump between headings |
| Copy code | Copy a code block |

## Links and limits

[Return to contents](index.html) or [jump to code examples](#code-examples).
Links and images are relative to the **help folder**, including in nested pages.
Heading anchors use lowercase words separated by hyphens; duplicate headings
receive a numeric suffix, such as `formatting-1`.

Raw HTML is shown literally: `<b>example</b>` is not interpreted as markup.
This is a documentation subset, not full CommonMark or GitHub Markdown.
Reference links, task checkboxes, footnotes, math, and embedded HTML are not
supported. Deeper headings use the third heading style. Ordered lists start at 1.
