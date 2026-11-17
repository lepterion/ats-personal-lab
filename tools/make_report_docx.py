#!/usr/bin/env python3
"""Converts docs/REPORT.md into docs/REPORT.docx (title page + sections).

Supports the subset of Markdown used in the report: "# " title, "## " sections,
paragraphs, "- " lists, ``` code blocks, | tables |, "> " quotes and inline
**bold**, *italic*, `code`. The lines between the "# " title and the first
"## " heading form the title page.

Requires python-docx:  pip install python-docx
Usage: python3 tools/make_report_docx.py docs/REPORT.md docs/REPORT.docx
"""
import re
import sys

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml.ns import qn
from docx.shared import Pt, Cm

FONT = "Times New Roman"
MONO = "Consolas"
INLINE = re.compile(r"(\*\*[^*]+\*\*|`[^`]+`|\*[^*\s][^*]*\*)")


def set_font(run, name, size=None):
    run.font.name = name
    run._element.rPr.rFonts.set(qn("w:eastAsia"), name)
    if size:
        run.font.size = Pt(size)


def add_inline(par, text, size=None):
    text = text.replace("\\|", "|")
    for part in INLINE.split(text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            run = par.add_run(part[2:-2])
            run.bold = True
            set_font(run, FONT, size)
        elif part.startswith("`") and part.endswith("`"):
            run = par.add_run(part[1:-1])
            set_font(run, MONO, (size or 12) - 1)
        elif part.startswith("*") and part.endswith("*") and len(part) > 2:
            run = par.add_run(part[1:-1])
            run.italic = True
            set_font(run, FONT, size)
        else:
            run = par.add_run(part)
            set_font(run, FONT, size)


def title_page(doc, lines):
    # lines: university, faculty, department, "", course, lab, topic, variant, "", author..., "", city
    lines = [l.strip() for l in lines]
    blocks, cur = [], []
    for l in lines:
        if l:
            cur.append(l)
        elif cur:
            blocks.append(cur)
            cur = []
    if cur:
        blocks.append(cur)
    header, middle, author, city = blocks[0], blocks[1], blocks[2], blocks[-1]
    for l in header:
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        add_inline(p, l, 14)
    for _ in range(6):
        doc.add_paragraph()
    for i, l in enumerate(middle):
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        add_inline(p, l, 16 if i == 2 else 14)
        if i == 2:
            for r in p.runs:
                r.bold = True
    for _ in range(6):
        doc.add_paragraph()
    for l in author:
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        add_inline(p, l, 14)
    for _ in range(8):
        doc.add_paragraph()
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    add_inline(p, city[0], 14)
    p.add_run().add_break(WD_BREAK.PAGE)


def add_table(doc, rows):
    cells = [[c.strip() for c in re.split(r"(?<!\\)\|", r.strip().strip("|"))] for r in rows]
    cells = [r for r in cells if not all(re.fullmatch(r":?-{3,}:?", c) for c in r)]
    table = doc.add_table(rows=len(cells), cols=len(cells[0]))
    table.style = "Table Grid"
    for i, row in enumerate(cells):
        for j, text in enumerate(row):
            par = table.cell(i, j).paragraphs[0]
            add_inline(par, text, 11)
            if i == 0:
                for r in par.runs:
                    r.bold = True
    doc.add_paragraph()


def main(src, dst):
    lines = open(src, encoding="utf-8").read().splitlines()
    doc = Document()
    for s in doc.sections:
        s.left_margin, s.right_margin = Cm(2.5), Cm(1.5)
        s.top_margin = s.bottom_margin = Cm(2)
    normal = doc.styles["Normal"]
    normal.font.name = FONT
    normal.font.size = Pt(12)
    normal.element.rPr.rFonts.set(qn("w:eastAsia"), FONT)

    first_section = next(i for i, l in enumerate(lines) if l.startswith("## "))
    title_page(doc, lines[1:first_section])

    i = first_section
    while i < len(lines):
        line = lines[i]
        if line.startswith("## "):
            h = doc.add_heading(level=1)
            run = h.add_run(line[3:])
            set_font(run, FONT, 16)
            i += 1
        elif line.startswith("```"):
            j = i + 1
            code = []
            while not lines[j].startswith("```"):
                code.append(lines[j])
                j += 1
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(0.5)
            run = p.add_run("\n".join(code))
            set_font(run, MONO, 10)
            i = j + 1
        elif line.startswith("|"):
            j = i
            while j < len(lines) and lines[j].startswith("|"):
                j += 1
            add_table(doc, lines[i:j])
            i = j
        elif line.startswith("- "):
            p = doc.add_paragraph(style="List Bullet")
            add_inline(p, line[2:])
            i += 1
        elif line.startswith("> "):
            p = doc.add_paragraph()
            p.paragraph_format.left_indent = Cm(1)
            add_inline(p, line[2:])
            for r in p.runs:
                r.italic = True
            i += 1
        elif re.match(r"^\d+\. ", line):
            p = doc.add_paragraph(style="List Number")
            add_inline(p, re.sub(r"^\d+\. ", "", line))
            i += 1
        elif line.strip() == "":
            i += 1
        else:
            p = doc.add_paragraph()
            p.paragraph_format.first_line_indent = Cm(1)
            add_inline(p, line)
            i += 1
    doc.save(dst)
    print(f"written {dst}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
