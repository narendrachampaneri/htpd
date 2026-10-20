#!/usr/bin/env python3
"""Creates the test PDFs in this folder.

NOT part of the HTPD build. The PDFs are already here; run this script only
if you want to change them:

    pip install reportlab
    python3 tests/data/make_test_pdfs.py
"""
import os

from reportlab import rl_config
from reportlab.lib import colors
from reportlab.lib.pagesizes import landscape, letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfgen import canvas
from reportlab.platypus import (BaseDocTemplate, Frame, NextPageTemplate, PageBreak,
                                PageTemplate, Paragraph, Spacer, Table, TableStyle)

rl_config.invariant = 1   # same bytes on every run (no timestamps)

OUT = os.path.dirname(os.path.abspath(__file__))
W, H = letter

body = ParagraphStyle('body', fontName='Helvetica', fontSize=10, leading=13, spaceAfter=7)
h1 = ParagraphStyle('h1', fontName='Helvetica-Bold', fontSize=16, leading=20, spaceBefore=14, spaceAfter=8)
h2 = ParagraphStyle('h2', fontName='Helvetica-Bold', fontSize=13, leading=16, spaceBefore=10, spaceAfter=6)
h3 = ParagraphStyle('h3', fontName='Helvetica-Bold', fontSize=11, leading=14, spaceBefore=8, spaceAfter=5)
cover = ParagraphStyle('cover', fontName='Helvetica-Bold', fontSize=28, leading=34, spaceAfter=12)
sub = ParagraphStyle('sub', fontName='Helvetica', fontSize=16, leading=20, spaceAfter=8)

LOREM = ('The controller supervises the power stage and the vehicle interface. It samples the bus voltage, '
         'the output current and three temperature sensors every 10 ms and reports the values over the '
         'internal CAN bus. All limits in this chapter apply over the full operating temperature range '
         'unless a different condition is stated in the table.')


class Manual(BaseDocTemplate):
    """A small manual with running headers/footers, three heading levels and a landscape appendix."""

    def __init__(self, path, bookmarks=True):
        super().__init__(path, pagesize=letter, leftMargin=72, rightMargin=72, topMargin=80, bottomMargin=72,
                         title='ACME DC Charger Controller Reference Manual', author='ACME Power')
        self.bookmarks = bookmarks
        self.bookmark_count = 0
        frame = Frame(72, 72, W - 144, H - 152, id='f', leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
        lw, lh = landscape(letter)
        wide = Frame(60, 60, lw - 120, lh - 130, id='lf', leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
        self.addPageTemplates([
            PageTemplate(id='cover', frames=[frame]),
            PageTemplate(id='main', frames=[frame], onPage=self.furniture),
            PageTemplate(id='wide', frames=[wide], pagesize=landscape(letter), onPage=self.furniture),
        ])

    def furniture(self, c, doc):
        w, h = c._pagesize
        margin = 72 if w < 700 else 60
        c.saveState()
        c.setFont('Helvetica', 8)
        c.drawString(margin, h - 40, 'ACME DC Charger Controller Reference Manual')
        c.drawRightString(w - margin, h - 40, 'Rev 1.2')
        c.line(60, h - 46, w - 60, h - 46)
        c.drawString(margin, 36, 'ACME Confidential')
        c.drawCentredString(w / 2, 36, 'Page %d' % doc.page)
        c.restoreState()

    def afterFlowable(self, flowable):
        if not self.bookmarks or not isinstance(flowable, Paragraph):
            return
        level = {'h1': 0, 'h2': 1, 'h3': 2}.get(flowable.style.name)
        if level is None:
            return
        self.bookmark_count += 1
        key = 'bm%d' % self.bookmark_count
        self.canv.bookmarkPage(key)
        self.canv.addOutlineEntry(flowable.getPlainText(), key, level=level, closed=False)


def manual_story():
    grid = [('GRID', (0, 0), (-1, -1), 0.5, colors.grey),
            ('FONT', (0, 0), (-1, 0), 'Helvetica-Bold', 9), ('FONT', (0, 1), (-1, -1), 'Helvetica', 9)]
    s = [Spacer(1, 150), Paragraph('ACME DC Charger Controller', cover), Paragraph('Reference Manual', sub),
         Paragraph('Document ACME-RM-042, Rev 1.2', body), NextPageTemplate('main'), PageBreak()]
    s += [Paragraph('Revision history', h1),
          Paragraph('Rev 1.2 adds the thermal derating rule and corrects the contactor ratings.', body),
          Paragraph('Rev 1.1 adds Appendix A.', body)]
    s += [Paragraph('1 Introduction', h1), Paragraph(LOREM, body),
          Paragraph('1.1 Scope', h2),
          Paragraph('This manual describes the 60 kW DC charger controller board, hardware revision C. '
                    'It covers electrical limits, the charging sequence and the register map.', body),
          Paragraph('1.2 Terms', h2),
          Paragraph('EVSE: electric vehicle supply equipment.', body, bulletText='•'),
          Paragraph('CCS: combined charging system.', body, bulletText='•'),
          Paragraph('The term inter-<br/>rupt latency means the time from the fault to the opening command.', body)]
    s += [Paragraph('2 Electrical characteristics', h1), Paragraph(LOREM, body),
          Paragraph('2.1 Supply', h2),
          Paragraph('The logic supply is 3.3 V. The contactor coil supply is 12 V.', body),
          Paragraph('2.1.1 Limits', h3)]
    limits = Table([['Parameter', 'Min', 'Typ', 'Max', 'Unit'],
                    ['Logic supply voltage', '3.0', '3.3', '3.6', 'V'],
                    ['Coil supply voltage', '10.8', '12.0', '13.2', 'V'],
                    ['Bus voltage', '200', '400', '480', 'V'],
                    ['Output current', '0', '', '200', 'A']], colWidths=[170, 60, 60, 60, 60])
    limits.setStyle(TableStyle(grid))
    s += [limits, Spacer(1, 8),
          Paragraph('2.2 Contactor', h2),
          Paragraph('The output contactor is rated for 200 A. It is controlled by the control board, which '
                    'receives its commands from the MCU.', body)]
    s += [Paragraph(LOREM, body) for _ in range(3)]
    s += [Paragraph('3 Charging sequence', h1)]
    s += [Paragraph(LOREM, body) for _ in range(4)]
    s += [Paragraph('3.1 Thermal derating', h2),
          Paragraph('If the temperature exceeds 85 °C, the controller shall reduce the charging current '
                    'unless emergency mode is enabled.', body),
          Paragraph('If the output current exceeds 180 A for more than 2 s, the controller shall open the '
                    'contactor.', body)]
    s += [Paragraph(LOREM, body) for _ in range(5)]
    s += [Paragraph('3.2 Shutdown', h2)]
    s += [Paragraph(LOREM, body) for _ in range(4)]
    s += [NextPageTemplate('wide'), PageBreak(),
          Paragraph('Appendix A Register map', h1),
          Paragraph('A.1 Status register', h2)]
    register = Table([['Bit', 'Name', 'Access', 'Reset', 'Description'],
                      ['0', 'READY', 'R', '0', 'Controller is ready to charge'],
                      ['1', 'FAULT', 'R', '0', 'A fault is latched'],
                      ['2', 'DERATE', 'R', '0', 'Thermal derating is active'],
                      ['7:3', 'Reserved', 'R', '0', 'Read as zero']], colWidths=[50, 90, 60, 60, 300])
    register.setStyle(TableStyle(grid))
    s += [register, Spacer(1, 8), Paragraph('Writing to a reserved bit has no effect.', body)]
    return s


def lowlevel(path):
    """Hand-placed text: the cases that break naive text extraction."""
    c = canvas.Canvas(path, pagesize=letter)

    # Page 1: scaled fonts, kerning, superscript, slanted text, rotated text.
    c.setFont('Helvetica', 12)
    c.drawString(72, 720, 'Plain twelve point line.')
    t = c.beginText()                       # font size 1, scaled 18x by the text matrix
    t.setFont('Helvetica-Bold', 1)
    t.setTextTransform(18, 0, 0, 18, 72, 680)
    t.textOut('Scaled heading eighteen')
    c.drawText(t)
    # Words separated only by kerning, no space characters.
    c._code.append('BT /F1 12 Tf 1 0 0 1 72 640 Tm [(Kerned) -400 (words) -400 (here)] TJ ET')
    c.setFont('Helvetica', 12)
    c.drawString(72, 600, 'Area is 5 m')
    x = 72 + c.stringWidth('Area is 5 m', 'Helvetica', 12)
    c.setFont('Helvetica', 7)
    c.drawString(x, 605, '2')               # superscript
    c.setFont('Helvetica', 12)
    c.drawString(x + 5, 600, ' in total.')
    t = c.beginText()                       # synthetic italic (sheared matrix)
    t.setFont('Helvetica', 12)
    t.setTextTransform(1, 0, 0.21, 1, 72, 560)
    t.textOut('Slanted synthetic italic line.')
    c.drawText(t)
    c.saveState(); c.translate(560, 300); c.rotate(90)
    c.setFont('Helvetica', 10); c.drawString(0, 0, 'VERTICAL SIDE LABEL'); c.restoreState()
    c.saveState(); c.translate(150, 250); c.rotate(45)
    c.setFont('Helvetica-Bold', 40); c.drawString(0, 0, 'DRAFT'); c.restoreState()
    c.setFont('Helvetica', 12)
    c.drawString(72, 520, '1111 and 11.11 stay together.')
    c.showPage()

    # Page 2: a /Rotate 90 page whose text reads upright on screen.
    c.setPageRotation(90)
    c.saveState(); c.translate(W, 0); c.rotate(90)
    c.setFont('Helvetica-Bold', 14); c.drawString(60, W - 80, 'Rotated page heading')
    c.setFont('Helvetica', 10)
    c.drawString(60, W - 110, 'First line on the rotated page.')
    c.drawString(60, W - 124, 'Second line on the rotated page.')
    c.restoreState()
    c.showPage()

    # Page 3: two columns.
    c.setPageRotation(0)
    c.setFont('Helvetica', 10)
    for i in range(6):
        c.drawString(72, 700 - 14 * i, 'Left column line %d continues here' % (i + 1))
    for i in range(6):
        c.drawString(330, 700 - 14 * i, 'Right column line %d continues here' % (i + 1))
    c.showPage()

    # Page 4: a code listing in a monospace font, with indentation and an empty line.
    c.setFont('Helvetica', 10)
    c.drawString(72, 720, 'The listing below shows the main loop.')
    c.setFont('Courier', 9)
    y = 700
    for line in ['int main(void) {', '    int state = 0;', '    if (state == 0) {', '        start();   // begin',
                 '    }', '', '    return state;', '}']:
        if line:
            c.drawString(72, y, line)
        y -= 11
    c.setFont('Helvetica', 10)
    c.drawString(72, y - 12, 'Set the file name in')
    c.setFont('Courier', 10)
    c.drawString(72, y - 26, 'charger.conf')            # one monospace line on its own: not a listing
    c.setFont('Helvetica', 10)
    c.drawString(72, y - 40, 'before the first start.')
    c.showPage()
    c.save()


def sideways(path):
    """Pages whose text runs sideways or upside down on the screen.

      1. A portrait page with a table printed sideways (text runs upwards),
         as datasheets print wide pin tables.
      2. A /Rotate 90 page whose text is upright in the page's own space, so
         it runs downwards on the screen.
      3. An upside-down page.
      4. An upright page with one vertical label: only the label is rotated text.
    """
    c = canvas.Canvas(path, pagesize=letter)
    c.saveState(); c.translate(0, 0); c.rotate(90)          # text runs upwards
    c.setFont('Helvetica-Bold', 12); c.drawString(100, -100, 'Pin table printed sideways')
    c.setFont('Helvetica', 10)
    for i in range(3):
        c.drawString(100, -130 - 16 * i, 'Ball A%d is GPIO%d and its supply is NVCC_GPIO' % (i + 1, i))
    c.restoreState()
    c.showPage()

    c.setPageRotation(90)                                    # upright content, page turned
    c.setFont('Helvetica', 10)
    c.drawString(72, 700, 'First line of the turned page.')
    c.drawString(72, 686, 'Second line of the turned page.')
    c.showPage()

    c.setPageRotation(0)
    c.saveState(); c.translate(W, H); c.rotate(180)          # upside down
    c.setFont('Helvetica', 10)
    c.drawString(72, 700, 'Upside down first line.')
    c.drawString(72, 686, 'Upside down second line.')
    c.restoreState()
    c.showPage()

    c.setFont('Helvetica', 10)
    for i in range(4):
        c.drawString(72, 700 - 14 * i, 'Upright body line %d of the last page.' % (i + 1))
    c.saveState(); c.translate(560, 300); c.rotate(90)
    c.setFont('Helvetica', 8); c.drawString(0, 0, 'SIDE LABEL'); c.restoreState()
    c.showPage()
    c.save()


def links(path):
    """Internal links, as manuals use them to jump to a register or a section.

      page 1  "1 Overview": "dmcontrol" links to 2.1 on page 2 (to a point just
              above that heading); a web link that is not a jump in the document.
      page 2  "2 Registers" at the top, "2.1 dmcontrol" in the middle; "Overview"
              links back to page 1.
      page 3  "3 Appendix": "Registers" links to page 2 as a whole (no point).
    """
    c = canvas.Canvas(path, pagesize=letter)

    def heading(text, y, key, level=0):
        c.setFont('Helvetica-Bold', 14 if level == 0 else 12)
        c.drawString(72, y, text)
        c.bookmarkHorizontalAbsolute(key, y + 16)
        c.addOutlineEntry(text, key, level)

    def link_words(before, words, y, key):
        x = 72 + c.stringWidth(before, 'Helvetica', 10)
        c.linkRect('', key, (x, y - 3, x + c.stringWidth(words, 'Helvetica', 10), y + 9), relative=0)

    heading('1 Overview', 720, 'ov')
    c.setFont('Helvetica', 10)
    c.drawString(72, 690, 'The reset bit is in dmcontrol, see there.')
    link_words('The reset bit is in ', 'dmcontrol', 690, 'dm')
    c.drawString(72, 676, 'More is at example.com on the web.')
    x = 72 + c.stringWidth('More is at ', 'Helvetica', 10)
    c.linkURL('https://example.com', (x, 673, x + 60, 685), relative=0)
    c.showPage()

    c.bookmarkPage('page2', fit='Fit')
    heading('2 Registers', 720, 'reg')
    c.setFont('Helvetica', 10)
    c.drawString(72, 690, 'The registers of the module follow.')
    heading('2.1 dmcontrol', 400, 'dm', 1)
    c.setFont('Helvetica', 10)
    c.drawString(72, 370, 'Controls the module. Back to the Overview.')
    link_words('Controls the module. Back to the ', 'Overview', 370, 'ov')
    c.showPage()

    heading('3 Appendix', 720, 'app')
    c.setFont('Helvetica', 10)
    c.drawString(72, 690, 'All of them are listed in Registers.')
    link_words('All of them are listed in ', 'Registers', 690, 'page2')
    c.showPage()
    c.save()


def raw_pdf(path, objects):
    """Writes a PDF file from the bodies of its objects (object 1 is the catalog)."""
    out = bytearray(b'%PDF-1.4\n')
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(out))
        out += b'%d 0 obj\n' % number + body + b'\nendobj\n'
    xref = len(out)
    out += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objects) + 1)
    for offset in offsets:
        out += b'%010d 00000 n \n' % offset
    out += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objects) + 1, xref)
    with open(path, 'wb') as f:
        f.write(out)


def fonts(path):
    """Fonts whose name and flags do not tell the truth about their widths.

    Written by hand, object by object, because the font dictionaries must be exact:
      F2 "ListingSans"      no fixed-pitch flag, nothing in the name, but every
                            character is 500 units wide: a monospace font.
      F3 "MonotypeCorsiva"  fixed-pitch flag set and "mono" in the name, but the
                            characters have the widths of Helvetica: not monospace.
    """
    helvetica = pdfmetrics.getFont('Helvetica').widths[32:127]

    def font(name, flags, widths, descriptor):
        return ('<< /Type /Font /Subtype /TrueType /BaseFont /%s /Encoding /WinAnsiEncoding '
                '/FirstChar 32 /LastChar 126 /Widths [%s] /FontDescriptor %d 0 R >>'
                % (name, ' '.join(str(w) for w in widths), descriptor)).encode('ascii'), \
               ('<< /Type /FontDescriptor /FontName /%s /Flags %d /FontBBox [-100 -250 1000 950] '
                '/ItalicAngle 0 /Ascent 900 /Descent -250 /CapHeight 700 /StemV 80 >>'
                % (name, flags)).encode('ascii')

    def show(font_name, size, x, y, text):
        escaped = text.replace('\\', '\\\\').replace('(', '\\(').replace(')', '\\)')
        return 'BT /%s %g Tf 1 0 0 1 %g %g Tm (%s) Tj ET\n' % (font_name, size, x, y, escaped)

    column = 0.5 * 9                    # width of one ListingSans character at 9 pt
    content = show('F1', 10, 72, 720, 'The listing below is set in a font whose name says nothing.')
    content += show('F2', 9, 72, 700, 'start:')
    content += show('F2', 9, 72, 689, '    mov   r0, #0        ; counter')   # indented by spaces
    content += show('F2', 9, 72, 678, 'loop:')
    content += show('F2', 9, 72, 667, '    add   r0, r0, #1')
    content += show('F2', 9, 72 + 4 * column, 656, 'b     loop')              # indented by position
    content += show('F3', 10, 72, 620, 'This note is set in a font that claims to be fixed pitch,')
    content += show('F3', 10, 72, 607, 'but its letters have different widths, so it is ordinary')
    content += show('F3', 10, 72, 594, 'text and its lines are joined into one paragraph.')
    stream = content.encode('ascii')

    listing_font, listing_descriptor = font('ListingSans', 32, [500] * 95, 6)      # 32 = nonsymbolic
    note_font, note_descriptor = font('MonotypeCorsiva', 33, helvetica, 8)         # 33 = + fixed pitch
    raw_pdf(path, [
        b'<< /Type /Catalog /Pages 2 0 R >>',
        b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
        b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 9 0 R '
        b'/Resources << /Font << /F1 4 0 R /F2 5 0 R /F3 7 0 R >> >> >>',
        b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>',
        listing_font, listing_descriptor, note_font, note_descriptor,
        b'<< /Length %d >>\nstream\n' % len(stream) + stream + b'endstream',
    ])


if __name__ == '__main__':
    Manual(os.path.join(OUT, 'sample.pdf'), bookmarks=True).build(manual_story())
    Manual(os.path.join(OUT, 'sample_nobookmarks.pdf'), bookmarks=False).build(manual_story())
    lowlevel(os.path.join(OUT, 'lowlevel.pdf'))
    fonts(os.path.join(OUT, 'fonts.pdf'))
    sideways(os.path.join(OUT, 'sideways.pdf'))
    links(os.path.join(OUT, 'links.pdf'))
    print('wrote sample.pdf, sample_nobookmarks.pdf, lowlevel.pdf, fonts.pdf, sideways.pdf, links.pdf to', OUT)
