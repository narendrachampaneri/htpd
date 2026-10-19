# Contributing to HTPD

Thank you for helping! HTPD is open source under the Apache License 2.0.

## How a change gets in

1. Fork the repository and create a branch.
2. Make your change, with tests (see below).
3. Open a pull request against `main` and fill in the checklist.
4. The maintainer reviews it. Nothing is merged into `main` without the
   maintainer's approval.

Small, focused pull requests are reviewed fastest. For a big change, open an
issue first so we can agree on the approach.

## Build and test

```sh
./scripts/get_pdfium.sh && make && make test     # the converter (C++23, g++ 13+)
python3 -m pip install -e . pytest && python3 -m pytest   # the reader (Python 3.10+)
```

## Rules

The full rules are in [`AGENTS.md`](AGENTS.md). The most important ones:

- All tests pass. Never change or delete a test just to make it pass.
- Every new rule in the code comes with a test that fails without it.
- Never lose text and never make text up; when unsure, keep the text as it stands.
- A change to what is written into an HTPD file updates `docs/FORMAT.md` in the same pull request.
- No new library without discussing it first. HTPD stays local, free and light.
- Do not commit private or confidential documents, or files of your own tools.

## License of contributions

By contributing you agree that your contribution is licensed under the
[Apache License 2.0](LICENSE), like the rest of HTPD.
