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
from reportlab.platypus import (SimpleDocTemplate, BaseDocTemplate, Frame, NextPageTemplate, PageBreak,
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


def ocr(path):
    """Text that is only in pictures: page 1 has text of its own and a picture
    of two lines of text; page 2 is a scanned page (one picture, no text);
    page 3 shows the picture of page 1 again."""
    from io import BytesIO
    from PIL import Image, ImageDraw, ImageFont
    from reportlab.lib.utils import ImageReader

    def picture(lines, width, height, size):
        img = Image.new('L', (width, height), 255)
        draw = ImageDraw.Draw(img)
        try:
            face = ImageFont.truetype('DejaVuSans.ttf', size)
        except OSError:
            face = ImageFont.load_default(size)
        for i, text in enumerate(lines):
            draw.text((40, 30 + i * int(size * 1.6)), text, fill=0, font=face)
        out = BytesIO()
        img.save(out, format='PNG')
        out.seek(0)
        return ImageReader(out)

    c = canvas.Canvas(path, pagesize=letter)
    c.setTitle('OCR test')
    c.setFont('Helvetica-Bold', 16)
    c.drawString(72, 720, '1 Wiring')
    c.setFont('Helvetica', 10)
    c.drawString(72, 700, 'The picture below shows the connector.')
    # 300 dpi: 4 x 1 inch on the page
    connector = picture(['Connector J7 pin 3 is CAN high.', 'Pin 4 is CAN low.'], 1200, 300, 48)
    c.drawImage(connector, 72, 560, width=288, height=72)
    c.drawString(72, 530, 'More text follows the picture.')
    c.showPage()
    c.drawImage(picture(['2 Scanned notes', 'The fuse is rated 16 A.', 'Replace it only when the power is off.'],
                        2550, 1100, 56), 0, 528, width=612, height=264)
    c.showPage()
    c.setFont('Helvetica', 10)
    c.drawString(72, 720, 'The connector picture again.')
    c.drawImage(connector, 72, 300, width=288, height=72)             # the same picture: read once
    c.showPage()
    c.save()


class Tagged:
    """A tagged PDF written by hand: pages with marked content, a structure tree
    of the standard types and a parent tree, as Word or LibreOffice write them.
    Text is Helvetica; y counts from the bottom of the page, as in a PDF."""

    class Elem:
        def __init__(self, kind, kids, attrs='', alt=None):
            self.kind, self.kids, self.attrs, self.alt = kind, kids, attrs, alt

    def __init__(self, pages=1):
        self.ops = [[] for _ in range(pages)]
        self.mcids = [0] * pages

    def _next(self, page):
        self.mcids[page] += 1
        return self.mcids[page] - 1

    def text(self, page, x, y, s, size=11, font='F1', tag='P'):
        """Tagged text; returns its marked-content ID (a leaf for elem())."""
        mcid = self._next(page)
        self.ops[page].append('/%s <</MCID %d>> BDC BT /%s %g Tf %g %g Td (%s) Tj ET EMC' % (tag, mcid, font, size, x, y, s))
        return ('mc', page, mcid)

    def plain(self, page, x, y, s, size=11, font='F1'):
        """Text without any tag: not part of the structure tree."""
        self.ops[page].append('BT /%s %g Tf %g %g Td (%s) Tj ET' % (font, size, x, y, s))

    def artifact(self, page, x, y, s, size=9, kind='Header'):
        self.ops[page].append('/Artifact <</Type /Pagination /Subtype /%s>> BDC BT /F1 %g Tf %g %g Td (%s) Tj ET EMC'
                              % (kind, size, x, y, s))

    def rect(self, page, x, y, w, h):
        """A filled rectangle that is a tagged piece of content (a drawing)."""
        mcid = self._next(page)
        self.ops[page].append('/Figure <</MCID %d>> BDC 0.8 0.85 0.95 rg %g %g %g %g re f 0 g EMC' % (mcid, x, y, w, h))
        return ('mc', page, mcid)

    def elem(self, kind, *kids, attrs='', alt=None):
        return Tagged.Elem(kind, [k for k in kids], attrs, alt)

    def write(self, path, document, bookmarks=()):
        """`bookmarks`: (title, page index) pairs for the outline."""
        pages = len(self.ops)
        objects = []                       # bodies, object number = index + 1

        def add(body):
            objects.append(body)
            return len(objects)

        add(b'')                                                  # 1: catalog (filled below)
        add(b'')                                                  # 2: pages
        add(b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
        add(b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>')
        page_numbers = []
        for index, ops in enumerate(self.ops):
            content = ('\n'.join(ops)).encode('latin-1')
            c = add(b'<< /Length %d >>\nstream\n' % len(content) + content + b'\nendstream')
            page_numbers.append(add(b''))
            objects[page_numbers[-1] - 1] = (
                b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents %d 0 R /StructParents %d '
                b'/Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> >>' % (c, index))
        root = add(b'')                                           # structure tree root
        parent_tree = add(b'')
        # number the elements, parents first
        flat = []

        def number(e, parent):
            e.number, e.parent = add(b''), parent
            flat.append(e)
            for k in e.kids:
                if isinstance(k, Tagged.Elem):
                    number(k, e)

        number(document, None)
        owner = {}                                                # (page, mcid) -> element
        for e in flat:
            body = b'<< /Type /StructElem /S /%s /P %d 0 R' % (e.kind.encode(), e.parent.number if e.parent else root)
            kids = []
            first_page = None
            for k in e.kids:
                if isinstance(k, Tagged.Elem):
                    kids.append(b'%d 0 R' % k.number)
                else:
                    _, pg, mcid = k
                    first_page = pg if first_page is None else first_page
                    owner[(pg, mcid)] = e
                    kids.append(b'<< /Type /MCR /Pg %d 0 R /MCID %d >>' % (page_numbers[pg], mcid))
            if first_page is not None:
                body += b' /Pg %d 0 R' % page_numbers[first_page]
            body += b' /K [' + b' '.join(kids) + b']'
            if e.attrs:
                body += b' /A << /O /Table ' + e.attrs.encode() + b' >>'
            if e.alt:
                body += b' /Alt (' + e.alt.encode() + b')'
            objects[e.number - 1] = body + b' >>'
        objects[root - 1] = b'<< /Type /StructTreeRoot /K [%d 0 R] /ParentTree %d 0 R >>' % (document.number, parent_tree)
        nums = b''
        for index in range(pages):
            refs = b' '.join(b'%d 0 R' % owner[(index, m)].number if (index, m) in owner else b'null'
                             for m in range(self.mcids[index]))
            nums += b'%d [%s] ' % (index, refs)
        objects[parent_tree - 1] = b'<< /Nums [%s] >>' % nums
        outline = b''
        if bookmarks:
            first = len(objects) + 2
            outline_root = add(b'')
            for k, (title, pg) in enumerate(bookmarks):
                add(b'<< /Title (%s) /Parent %d 0 R /Dest [%d 0 R /Fit]%s%s >>' % (
                    title.encode(), outline_root, page_numbers[pg],
                    b' /Prev %d 0 R' % (first + k - 1) if k else b'',
                    b' /Next %d 0 R' % (first + k + 1) if k + 1 < len(bookmarks) else b''))
            objects[outline_root - 1] = b'<< /Type /Outlines /First %d 0 R /Last %d 0 R /Count %d >>' % (
                first, first + len(bookmarks) - 1, len(bookmarks))
            outline = b' /Outlines %d 0 R' % outline_root
        objects[0] = (b'<< /Type /Catalog /Pages 2 0 R /StructTreeRoot %d 0 R /MarkInfo << /Marked true >> /Lang (en-US)%s >>'
                      % (root, outline))
        objects[1] = b'<< /Type /Pages /Kids [%s] /Count %d >>' % (b' '.join(b'%d 0 R' % n for n in page_numbers), pages)
        raw_pdf(path, objects)


def tagged(path, bookmarks=False):
    """A tagged PDF whose headings are set in the body font (only the tags tell
    they are headings): H1, H2, a table with a cell of three lines, an empty cell the
    tags leave out, a cell over two columns and one over two rows, a list whose
    labels are no list marks, a drawing with a caption, and a running header and
    footer marked as artifacts - on two pages."""
    t = Tagged(2)
    for page, header in ((0, 'Charger overview sheet'), (1, 'Safety summary 2024')):   # a header that differs on every page
        t.artifact(page, 72, 760, header, kind='Header')
        t.artifact(page, 280, 30, 'Page %d' % (page + 1), kind='Footer')
    body = []
    body.append(t.elem('H1', t.text(0, 72, 715, 'Introduction', tag='H1')))
    body.append(t.elem('P', t.text(0, 72, 695, 'This charger has two connectors and one safety controller.')))
    body.append(t.elem('H2', t.text(0, 72, 665, 'Scope', tag='H2')))
    body.append(t.elem('P', t.text(0, 72, 645, 'The limits below apply over the full temperature range'),
                       ))
    # the table: columns at x = 72, 190, 330, 430
    xs = (72, 190, 330, 430)

    def cell(kind, x, lines, y, **kw):
        return t.elem(kind, t.elem('P', *[t.text(0, x, y - 12 * i, line) for i, line in enumerate(lines)]), **kw)

    rows = []
    y = 610
    rows.append(t.elem('TR', *[cell('TH', x, [name], y) for x, name in zip(xs, ('Name', 'Description', 'Min', 'Max'))]))
    y -= 16
    rows.append(t.elem('TR', cell('TD', xs[0], ['VIN'], y),
                       cell('TD', xs[1], ['Supply voltage of the', 'main power stage', 'at the connector'], y),
                       cell('TD', xs[2], ['-0.5'], y), cell('TD', xs[3], ['32'], y)))
    y -= 40
    rows.append(t.elem('TR', cell('TD', xs[0], ['IOUT'], y), cell('TD', xs[1], ['Output current'], y),
                       cell('TD', xs[3], ['2.5'], y)))               # the empty Min cell is left out of the tags
    y -= 16
    rows.append(t.elem('TR', cell('TD', xs[0], ['Combined limits'], y, attrs='/ColSpan 2'),
                       cell('TD', xs[2], ['-1'], y), cell('TD', xs[3], ['40'], y)))
    y -= 16
    rows.append(t.elem('TR', cell('TD', xs[0], ['Group'], y, attrs='/RowSpan 2'), cell('TD', xs[1], ['Alpha'], y),
                       cell('TD', xs[2], ['1'], y), cell('TD', xs[3], ['2'], y)))
    y -= 16
    rows.append(t.elem('TR', cell('TD', xs[1], ['Beta'], y), cell('TD', xs[2], ['3'], y), cell('TD', xs[3], ['4'], y)))
    body.append(t.elem('Table', *rows))
    # a list: labels that are no list marks
    y = 470
    items = []
    for label, lines in (('A.', ['Connect the vehicle']), ('B.', ['Check the lock and', 'the cable temperature']), ('C.', ['Start charging'])):
        items.append(t.elem('LI', t.elem('Lbl', t.text(0, 72, y, label)),
                            t.elem('LBody', t.elem('P', *[t.text(0, 100, y - 12 * i, line) for i, line in enumerate(lines)]))))
        y -= 12 * len(lines) + 4
    body.append(t.elem('L', *items))
    # a drawing with a caption that does not start with "Figure"
    body.append(t.elem('Figure', t.rect(0, 72, 290, 150, 80), alt='Block diagram of the power path'))
    body.append(t.elem('Caption', t.text(0, 72, 270, 'Power path of the charger', size=9, tag='Caption')))
    # page 2
    body.append(t.elem('H2', t.text(1, 72, 715, 'Details', tag='H2')))
    body.append(t.elem('P', t.text(1, 72, 695, 'Every value in this manual is measured at 25 degrees.')))
    t.write(path, t.elem('Document', *body),
            bookmarks=(('Introduction', 0), ('Scope', 0), ('Details', 1)) if bookmarks else ())


def tagged_disagree(path):
    """Tags that do not fit the page: a table with text lying on it that the tags
    do not name, a heading tag on a whole paragraph, a list item whose text is
    split by other text, a table whose rows are tagged in the wrong order, a
    table with other text stored in the middle of it, a short row whose cell
    lies between two columns, a cell over rows in a short row, a table of one
    column, a table inside a table, and (page 2) one piece of text named twice
    in the tree. An artifact in the middle of the page is not furniture. The
    converter must read all of these as if the tags were not there."""
    from reportlab.pdfbase.pdfmetrics import stringWidth
    t = Tagged(2)
    body = []
    body.append(t.elem('P', t.text(0, 72, 720, 'Opening paragraph of the page.')))
    # 1. a table with an untagged word lying inside it
    cells = []
    for i, (a, b, c) in enumerate((('Pin', 'Name', 'Use'), ('1', 'VCC', 'Supply'), ('2', 'GND', 'Ground'))):
        y = 690 - 16 * i
        cells.append(t.elem('TR', *[t.elem('TD', t.elem('P', t.text(0, x, y, v))) for x, v in ((72, a), (180, b), (290, c))]))
    t.plain(0, 190, 682, 'stray')
    body.append(t.elem('Table', *cells))
    # 2. a paragraph tagged as a heading
    long_text = ('This is a long paragraph that was tagged as a heading by mistake, so the converter should not '
                 'turn it into a heading of the document, because a heading is short and this text is not short '
                 'at all, it goes on and on about the charger, its connectors, its controller and its limits, and '
                 'it has more than three hundred characters in it altogether.')
    body.append(t.elem('H1', t.text(0, 72, 620, long_text[:100], tag='H1'), t.text(0, 72, 608, long_text[100:200], tag='H1'),
                       t.text(0, 72, 596, long_text[200:], tag='H1')))
    # 3. a list item whose two halves have other text between them
    first = t.text(0, 72, 560, 'Item that is split')
    t.plain(0, 72, 548, 'Text that stands between the halves of the item.')
    second = t.text(0, 72, 536, 'into two parts on the page')
    body.append(t.elem('L', t.elem('LI', t.elem('LBody', t.elem('P', first, second)))))
    # 4. a table whose rows are tagged in the opposite order of the page
    rows = []
    for i, (a, b) in enumerate((('Top row', 'first'), ('Middle row', 'second'), ('Bottom row', 'third'))):
        y = 480 - 16 * i
        rows.append((y, a, b))
    trs = [t.elem('TR', *[t.elem('TD', t.elem('P', t.text(0, x, y, v))) for x, v in ((72, a), (250, b))]) for y, a, b in rows]
    body.append(t.elem('Table', *reversed(trs)))
    # 5. a table with unrelated text stored in the middle of it (away from it on the page)
    first_row = t.elem('TR', *[t.elem('TD', t.elem('P', t.text(0, x, 400, v))) for x, v in ((72, 'Left one'), (250, 'Right one'))])
    t.plain(0, 450, 380, 'A note in the margin')
    second_row = t.elem('TR', *[t.elem('TD', t.elem('P', t.text(0, x, 384, v))) for x, v in ((72, 'Left two'), (250, 'Right two'))])
    body.append(t.elem('Table', first_row, second_row))

    def row(y, *cells_at, attrs=''):
        return t.elem('TR', *[t.elem('TD', t.elem('P', t.text(0, x, y, v)), attrs=a) for x, v, a in cells_at])

    # 6. a short row whose cell lies half in one column and half in the next
    wide = 'Nine columns wide'
    x0 = 163 - stringWidth(wide, 'Helvetica', 11) / 2          # its middle between the columns at 100 and 200
    body.append(t.elem('Table',
                       row(340, (100, 'Aaaa', ''), (200, 'Bbbb', ''), (330, 'Cccc', '')),
                       row(326, (100, 'Dddd', ''), (200, 'Eeee', ''), (330, 'Ffff', '')),
                       row(312, (x0, wide, ''), (330, 'Gggg', ''))))
    # 7. a cell over two rows that starts in a short row
    body.append(t.elem('Table',
                       row(280, (72, 'One', ''), (200, 'Two', ''), (330, 'Three', '')),
                       row(266, (200, 'Spanning', '/RowSpan 2'), (330, 'Four', '')),
                       row(252, (330, 'Five', ''))))
    # 8. a table of one column
    body.append(t.elem('Table', row(220, (72, 'Alone', '')), row(206, (72, 'Still alone', ''))))
    # 9. a table inside a table
    inner = t.elem('Table', row(176, (72, 'Inner one', ''), (160, 'Inner two', '')))
    body.append(t.elem('Table', t.elem('TR', t.elem('TD', inner), t.elem('TD', t.elem('P', t.text(0, 300, 176, 'Outer cell'))))))
    # an artifact in the middle of the page is not furniture
    t.artifact(0, 400, 300, 'Stamp in the middle', kind='Layout')
    # page 2: the same piece of text is named twice by one heading
    claimed = t.text(1, 72, 720, 'Claimed twice')
    body.append(t.elem('H1', claimed, claimed))
    body.append(t.elem('P', t.text(1, 72, 700, 'Other text of the second page.')))
    t.write(path, t.elem('Document', *body))

def figures(path):
    """Figures and things that are not figures: a block diagram drawn with
    lines and its caption, a ruled table, a framed listing, a photo without a
    caption and a logo in the page margin."""
    from io import BytesIO
    from PIL import Image, ImageDraw
    from reportlab.lib.utils import ImageReader

    def photo(width, height):
        img = Image.new('RGB', (width, height), (230, 240, 250))
        draw = ImageDraw.Draw(img)
        for i in range(0, width, 20):
            draw.line([(i, 0), (width - i, height)], fill=(40 + i % 200, 90, 160), width=3)
        out = BytesIO()
        img.save(out, format='PNG')
        out.seek(0)
        return ImageReader(out)

    c = canvas.Canvas(path, pagesize=letter)
    c.setTitle('Figure test')
    c.drawImage(photo(120, 40), 470, 750, width=72, height=24)          # a logo in the margin
    c.setFont('Helvetica-Bold', 16)
    c.drawString(72, 720, '1 Power path')
    c.setFont('Helvetica', 10)
    c.drawString(72, 700, 'The figure below shows how power flows from the grid to the battery.')
    # a block diagram: boxes, lines and arrow heads, with labels
    boxes = [(80, 560, 'Grid'), (230, 560, 'Rectifier'), (380, 560, 'Battery')]
    for x, y, label in boxes:
        c.rect(x, y, 100, 50)
        c.rect(x + 4, y + 4, 92, 42)
        c.drawCentredString(x + 50, y + 22, label)
    for x in (180, 330):
        c.line(x, 585, x + 50, 585)
        c.line(x + 50, 585, x + 42, 590)
        c.line(x + 50, 585, x + 42, 580)
        c.line(x + 20, 575, x + 30, 595)
    c.setFont('Helvetica-Oblique', 9)
    c.drawString(72, 540, 'Figure 1. Power path of the charger')
    c.setFont('Helvetica', 10)
    c.drawString(72, 515, 'Table 1. Limits')
    rows = [('Parameter', 'Min', 'Max'), ('Voltage', '200', '480'), ('Current', '0', '32'), ('Power', '0', '22')]
    for i, row in enumerate(rows):
        y = 495 - i * 18
        for j, cell in enumerate(row):
            c.drawString(80 + j * 120, y, cell)
    for i in range(len(rows) + 1):
        c.line(72, 508 - i * 18, 432, 508 - i * 18)
    for x in (72, 192, 312, 432):
        c.line(x, 508, x, 508 - len(rows) * 18)
    c.setFont('Courier', 9)
    for i, line in enumerate(['int main(void) {', '    start();', '    return 0;', '}']):
        c.drawString(84, 380 - i * 12, line)
    c.rect(76, 336, 300, 56)
    for k in range(10):
        c.line(76 + k * 30, 336, 76 + k * 30, 334)                       # ticks under the frame: more shapes
    c.setFont('Helvetica', 10)
    c.drawString(72, 300, 'A photo of the charger follows.')
    picture = photo(400, 200)
    c.drawImage(picture, 72, 120, width=240, height=120)
    c.drawString(72, 100, 'The text ends here.')
    c.showPage()
    c.setFont('Helvetica', 10)
    c.drawString(72, 720, 'The same photo again, on the second page.')
    c.drawImage(picture, 72, 560, width=240, height=120)                # stored once in the PDF
    c.showPage()
    c.save()


def actualtext(path):
    """Text marked with /ActualText: what the glyphs mean (complex scripts, ligatures)."""
    c = canvas.Canvas(path, pagesize=letter)
    c.setFont('Helvetica', 12)
    c.drawString(72, 720, 'Plain words first.')
    # The glyphs say "XYZ"; the span says they mean "Hello".
    c._code.append('BT /F1 12 Tf 1 0 0 1 72 690 Tm (Before ) Tj ET '
                   '/Span <</ActualText (Hello)>> BDC BT /F1 12 Tf 1 0 0 1 116 690 Tm (XYZ) Tj ET EMC '
                   'BT /F1 12 Tf 1 0 0 1 150 690 Tm ( after.) Tj ET')
    # UTF-16 with a byte order mark: Gujarati, drawn with any glyphs.
    c._code.append('/Span <</ActualText <FEFF0A9A0ABE0AB00ACD0A9C0AB0>>> BDC BT /F1 12 Tf 1 0 0 1 72 660 Tm (abcd) Tj ET EMC')
    c.showPage()
    c.save()


def tagged_styled(path):
    """A tagged PDF whose headings are also set apart by their look (bold, larger), with no bookmarks and
    no /Title: the text alone gives good headings, so the tags are not needed for them. A Title element
    names the document; H1 and H2 give two levels; a table has the tags' cells."""
    t = Tagged(2)
    para = ('The charger supervises the power stage and the vehicle interface and reports the values',
            'over the internal bus every ten milliseconds, as long as a vehicle is connected and',
            'the safety controller has released the output contactor of the power stage.')
    t.plain(0, 72, 770, 'ACME POWER', size=24, font='F2')       # a name in the largest type that is no title
    body = [t.elem('Title', t.text(0, 72, 745, 'Charger overview sheet', size=20, font='F2', tag='Title'))]
    y = 715
    for page, heads in ((0, (('H1', 'Introduction', 16), ('H2', 'Scope', 13), ('H2', 'Limits', 13))),
                        (1, (('H1', 'Details', 16), ('H2', 'Wiring', 13)))):
        y = 715 if page == 0 else 745
        for kind, title, size in heads:
            body.append(t.elem(kind, t.text(page, 72, y, title, size=size, font='F2', tag=kind)))
            y -= size + 12
            body.append(t.elem('P', *[t.text(page, 72, y - 14 * k, line) for k, line in enumerate(para)]))
            y -= 14 * len(para) + 20
    t.write(path, t.elem('Document', *body))


PARA = ('The controller supervises the power stage and the vehicle interface, samples the bus voltage and the '
        'output current every ten milliseconds and reports the values over the internal bus. All limits in this '
        'part apply over the full operating temperature range unless a different condition is stated.')


def styles_weight(path):
    """Headings that differ from the text by weight or by colour only: the same size, the same font.
    Level 1 is bold (Helvetica-Bold 10), level 2 is regular but blue (Helvetica 10). Nothing else tells
    them from body text: no number, no larger size, no bookmark."""
    c = canvas.Canvas(path, pagesize=letter)
    c.setTitle('Style sheet test: weight and colour')
    c.setProducer('ReportLab test')
    blue = colors.Color(0.1, 0.2, 0.7)
    topics = [('Overview', ['Purpose', 'Audience']), ('Operation', ['Start up', 'Shut down']),
              ('Maintenance', ['Cleaning', 'Fuses']), ('Warranty', ['Terms', 'Claims'])]
    page = 0
    y = H - 72
    for title, subs in topics:
        for head, size_font, colour in [(title, 'Helvetica-Bold', colors.black)] + [(s, 'Helvetica', blue) for s in subs]:
            if y < 200:
                c.showPage()
                y = H - 72
            c.setFont(size_font, 10)
            c.setFillColor(colour)
            c.drawString(72, y, head)
            y -= 16
            c.setFillColor(colors.black)
            c.setFont('Helvetica', 10)
            words = PARA.split()
            for k in range(3):
                c.drawString(72, y, ' '.join(words[k * 9:(k + 1) * 9 + 3]))
                y -= 13
            y -= 10
    c.showPage()
    c.save()


def latex_chapters(path):
    """A LaTeX report without bookmarks: every chapter starts with the line "Chapter N" above its title."""
    c = canvas.Canvas(path, pagesize=letter)
    c.setTitle('Chapter page test')
    c.setCreator('LaTeX with hyperref')
    c.setProducer('pdfTeX-1.40.25')
    chapters = [('Introduction', ['Scope', 'Terms']), ('Method', ['Setup', 'Procedure']), ('Results', ['Data', 'Errors']),
                ('Discussion', ['Limits', 'Outlook'])]
    for number, (title, subs) in enumerate(chapters, 1):
        y = H - 150
        c.setFont('Helvetica-Bold', 17)
        c.drawString(72, y, 'Chapter %d' % number)
        y -= 40
        c.setFont('Helvetica-Bold', 25)
        c.drawString(72, y, title)
        y -= 50
        for k, sub in enumerate(subs, 1):
            c.setFont('Helvetica-Bold', 14)
            c.drawString(72, y, '%d.%d %s' % (number, k, sub))
            y -= 22
            c.setFont('Helvetica', 10)
            words = PARA.split()
            for j in range(4):
                c.drawString(72, y, ' '.join(words[j * 8:(j + 1) * 8 + 3]))
                y -= 13
            y -= 14
        c.setFont('Helvetica', 10)
        c.drawCentredString(W / 2, 50, str(number))
        c.showPage()
    c.save()


def chrome_template(path, pages=1):
    """A web page printed by Chrome with its default header and footer: date and title above, address and
    "n/N" below. One page has nothing that repeats, so only the shape of the template says what it is."""
    c = canvas.Canvas(path, pagesize=letter)
    c.setTitle('Chrome print test')
    c.setCreator('Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) HeadlessChrome/153.0.0.0 Safari/537.36')
    c.setProducer('Skia/PDF m153')
    for n in range(1, pages + 1):
        c.setFont('Helvetica', 8)
        c.drawString(36, H - 30, '10/24/26, 5:10 AM Charger status page')
        c.drawString(36, 30, 'https://example.org/status')
        c.drawRightString(W - 36, 30, '%d/%d' % (n, pages))
        c.setFont('Helvetica-Bold', 18)
        c.drawString(72, H - 100, 'Charger status')
        c.setFont('Helvetica', 11)
        y = H - 130
        for line in PARA.split('. ')[:2] * 2:
            c.drawString(72, y, line[:90])
            y -= 15
        c.showPage()
    c.save()


def ruled_tables(path):
    """Tables drawn with lines: one with cells over rows and columns, one of two columns, and a chart
    with grid lines that is no table (the lattice method of the table engine)."""
    from reportlab.graphics.shapes import Drawing, Line, String
    cell = ParagraphStyle('cell', fontName='Helvetica', fontSize=9, leading=11)
    grid = [('GRID', (0, 0), (-1, -1), 0.5, colors.black), ('VALIGN', (0, 0), (-1, -1), 'TOP'),
            ('TOPPADDING', (0, 0), (-1, -1), 3), ('BOTTOMPADDING', (0, 0), (-1, -1), 3)]
    merged = Table([['Region', 'Area', 'Sales', '', ''], ['', '', 'Q1', 'Q2', 'Q3'],
                    ['North', 'Urban', '10', '11', '12'], ['', 'Rural', '5', '6', '7'],
                    ['South', 'Urban', '20', '22', '21'], ['', 'Rural', '8', '9', '9']],
                   colWidths=[60, 60, 50, 50, 50])
    merged.setStyle(TableStyle(grid + [('SPAN', (0, 0), (0, 1)), ('SPAN', (1, 0), (1, 1)), ('SPAN', (2, 0), (4, 0)),
                                       ('SPAN', (0, 2), (0, 3)), ('SPAN', (0, 4), (0, 5))]))
    pairs = Table([[Paragraph('Operating voltage', cell), Paragraph('The controller works from 3.0 V to 3.6 V and '
                                                                   'switches itself off below 2.7 V.', cell)],
                   [Paragraph('Protection class', cell), Paragraph('IP20', cell)]], colWidths=[110, 230])
    pairs.setStyle(TableStyle(grid))
    chart = Drawing(300, 140)
    for i in range(5):
        chart.add(Line(30, 20 + i * 25, 290, 20 + i * 25, strokeColor=colors.grey, strokeWidth=0.4))
        chart.add(String(10, 17 + i * 25, str(i * 10), fontName='Helvetica', fontSize=8))
    for i in range(6):
        chart.add(Line(30 + i * 52, 20, 30 + i * 52, 120, strokeColor=colors.grey, strokeWidth=0.4))
    doc = SimpleDocTemplate(path, pagesize=letter, leftMargin=72, rightMargin=72, topMargin=72, bottomMargin=72,
                            title='Ruled tables', author='HTPD tests')
    doc.build([Paragraph('Ruled tables', h1), Paragraph('Sales by region and quarter.', body), merged, Spacer(1, 14),
               Paragraph('Operating data.', body), pairs, Spacer(1, 14),
               Paragraph('Temperature over a day.', body), chart])


def multipage_table(path):
    """One table over three pages with its header printed again on each, a second table of the same
    columns on the last page that is no continuation (it starts under a heading and a paragraph), and a
    table of four columns on page 1 (the table engine joins the parts of a table over a page break)."""
    rows = [['Part', 'Name', 'Qty', 'Price']] + [['P%04d' % i, 'Washer %d' % (i % 37 + 2), str(i * 7 % 900 + 1),
                                                   '%d.%02d' % (i * 3 % 99 + 1, i * 11 % 100)] for i in range(1, 121)]
    t = Table(rows, colWidths=[60, 120, 50, 60], repeatRows=1)
    t.setStyle(TableStyle([('LINEABOVE', (0, 0), (-1, 0), 1, colors.black), ('LINEBELOW', (0, 0), (-1, 0), 0.6, colors.black),
                           ('LINEBELOW', (0, -1), (-1, -1), 1, colors.black), ('FONTSIZE', (0, 0), (-1, -1), 9),
                           ('TOPPADDING', (0, 0), (-1, -1), 2), ('BOTTOMPADDING', (0, 0), (-1, -1), 2)]))
    other = Table([['Part', 'Name', 'Qty', 'Price'], ['X1', 'Gasket', '4', '1.10'], ['X2', 'Seal', '9', '0.35']],
                  colWidths=[60, 120, 50, 60])
    other.setStyle(TableStyle([('FONTSIZE', (0, 0), (-1, -1), 9)]))
    doc = SimpleDocTemplate(path, pagesize=letter, leftMargin=72, rightMargin=72, topMargin=72, bottomMargin=72,
                            title='Parts list', author='HTPD tests')
    doc.build([Paragraph('Parts list', h1), Paragraph('All parts of the charger.', body), t, Spacer(1, 18),
               Paragraph('Spare parts', h2), Paragraph('The spare parts kit holds:', body), other])


def reading_order(path):
    """Page 1: two columns stored line by line (a line of the left column, a line of the right one, ...).
    Page 2: one column of text with a framed box stored in the middle of a sentence."""
    import textwrap
    left = textwrap.wrap('The charger controller samples the bus voltage every ten milliseconds and reports the value over '
                         'the internal bus. When the value leaves the allowed range for more than three samples the '
                         'controller opens the contactor and writes a fault code into the status register of the unit. '
                         'The fault code stays set until a host clears it by hand.', 44)
    right = textwrap.wrap('Output current is measured by a shunt resistor in the return path and filtered before the '
                          'converter reads it. The filter has a corner frequency of two hundred hertz which keeps the '
                          'ripple of the stage out of the result and makes the reading stable enough for the limit '
                          'checks of the control loop that runs every single millisecond.', 44)
    c = canvas.Canvas(path, pagesize=letter, pageCompression=0)
    c.setTitle('Reading order')
    c.setFont('Helvetica', 10)
    import itertools
    for i, (l, r) in enumerate(itertools.zip_longest(left, right)):
        y = H - 100 - 13 * i
        if l:
            c.drawString(72, y, l)
        if r:
            c.drawString(330, y, r)
    c.showPage()
    lines = ['The supply stage is checked at start-up and again every',
             'second while the charger runs. A reading outside the limits is',
             'reported to the host and, when it lasts for more than the',
             'filter time, switches the output off before the stage and the',
             'cable can be damaged by the current that flows into the',
             'fault. After that the controller waits for the host.']
    c.setFont('Helvetica', 10)
    for i, ln in enumerate(lines[:3]):
        c.drawString(72, H - 100 - 13 * i, ln)
    c.rect(80, H - 190, 380, 40, stroke=1, fill=0)
    c.setFont('Helvetica-Bold', 9)
    c.drawString(88, H - 166, 'Note on the limits')
    c.setFont('Helvetica', 9)
    c.drawString(88, H - 180, 'The limits are given in the table of the data sheet.')
    c.setFont('Helvetica', 10)
    for i, ln in enumerate(lines[3:]):
        c.drawString(72, H - 215 - 13 * i, ln)
    c.showPage()
    c.save()
    # What a person reads, page by page (tests/bench/layout_score.py scores the converter against it).
    import json
    truth = {'case': 'reading_order', 'pages': [
        {'page': 1, 'paragraphs': [{'id': 'left', 'kind': 'body', 'text': ' '.join(left)},
                                   {'id': 'right', 'kind': 'body', 'text': ' '.join(right)}]},
        {'page': 2, 'paragraphs': [{'id': 'body', 'kind': 'body', 'text': ' '.join(lines)},
                                   {'id': 'box', 'kind': 'box',
                                    'text': 'Note on the limits The limits are given in the table of the data sheet.'}]}]}
    with open(path.replace('.pdf', '.truth.json'), 'w', encoding='utf-8') as f:
        json.dump(truth, f, indent=1)
        f.write('\n')


if __name__ == '__main__':
    Manual(os.path.join(OUT, 'sample.pdf'), bookmarks=True).build(manual_story())
    Manual(os.path.join(OUT, 'sample_nobookmarks.pdf'), bookmarks=False).build(manual_story())
    lowlevel(os.path.join(OUT, 'lowlevel.pdf'))
    fonts(os.path.join(OUT, 'fonts.pdf'))
    sideways(os.path.join(OUT, 'sideways.pdf'))
    links(os.path.join(OUT, 'links.pdf'))
    ocr(os.path.join(OUT, 'ocr.pdf'))
    figures(os.path.join(OUT, 'figures.pdf'))
    actualtext(os.path.join(OUT, 'actualtext.pdf'))
    tagged(os.path.join(OUT, 'tagged.pdf'))
    tagged(os.path.join(OUT, 'tagged_bookmarks.pdf'), bookmarks=True)
    tagged_disagree(os.path.join(OUT, 'tagged_disagree.pdf'))
    styles_weight(os.path.join(OUT, 'styles_weight.pdf'))
    latex_chapters(os.path.join(OUT, 'latex_chapters.pdf'))
    chrome_template(os.path.join(OUT, 'chrome_template.pdf'))
    tagged_styled(os.path.join(OUT, 'tagged_styled.pdf'))
    ruled_tables(os.path.join(OUT, 'ruled_tables.pdf'))
    multipage_table(os.path.join(OUT, 'multipage_table.pdf'))
    reading_order(os.path.join(OUT, 'reading_order.pdf'))
    print('wrote sample.pdf, sample_nobookmarks.pdf, lowlevel.pdf, fonts.pdf, sideways.pdf, links.pdf, ocr.pdf, figures.pdf, actualtext.pdf, tagged.pdf, tagged_bookmarks.pdf, tagged_disagree.pdf, styles_weight.pdf, latex_chapters.pdf, chrome_template.pdf, tagged_styled.pdf, ruled_tables.pdf, multipage_table.pdf, reading_order.pdf to', OUT)
