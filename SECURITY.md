# Security policy

HTPD reads PDF files, which may come from untrusted sources. If you find a
security problem - for example a PDF that crashes the converter in a way that
could be exploited, or text in a document that makes the reader or the skill
do something it should not - please **do not open a public issue**.

Report it privately through GitHub: on the repository page, open the
**Security** tab and choose **Report a vulnerability**. You will get an answer
as soon as possible, and credit for the report if you want it.

Keep the PDF engine up to date: `PDFIUM_VERSION=latest ./scripts/get_pdfium.sh`.
