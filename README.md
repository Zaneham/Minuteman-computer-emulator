# Minuteman Guidance Computer Emulator

An emulator for the D17B/D37C guidance computers that have been keeping 400 nuclear missiles pointed at things since 1962. You know, for fun.

## What Is This?

This is a cycle-approximate emulator for the Autonetics D17B and D37C guidance computers used in the LGM-30 Minuteman intercontinental ballistic missile system.

The D17B powered Minuteman I. The D37C powers Minuteman III, which remains in active service today. Yes, *today*. The missiles currently sitting in silos across Montana, Wyoming, and North Dakota run on a computer architecture designed when the Beatles were still playing clubs in Hamburg.

We are not making this up.

## Features

- **Full D17B instruction set** (39 instructions) - Minuteman I, 1962
- **Full D37C instruction set** (57 instructions) - Minuteman II/III, 1965-present
- **Hardware division** - The D37C added this. The D17B had to do division in software. Imagine.
- **Rotating disc memory simulation** - 6000 RPM, because why would you use RAM when you could use a spinning magnetic disc?
- **Rapid-access loops** - U(1), F(4), E(8), H(16) words of "fast" memory
- **24-bit words** - Not 8, not 16, not 32. Twenty-four. Obviously.

## Architecture

```
Word Size:       24 bits (sign-magnitude, naturally)
Memory:          D17B: 2,944 words / D37C: 7,222 words
Storage:         Rotating magnetic disc @ 6000 RPM
Clock:           ~12,800 instructions/second (weather permitting)
Weight:          D17B: 28kg / D37C: 15kg
Power:           28V DC, 400Hz AC (missile-supplied)
```

The computer uses a rotating disc for memory. Not a hard drive - there's no seeking. Just a disc spinning at 6000 RPM with fixed read/write heads. Memory access time depends on where the disc happens to be when you ask for something. Programming requires you to think about where on the disc your data physically is.

This is called "minimum latency coding" and it is exactly as tedious as it sounds.

## Building

```bash
make
```

If that doesn't work, you may need to install a C compiler. We recommend any compiler from after 1970.

## Usage

```bash
# Run the test suite
./d17b -t

# Interactive mode
./d17b -i
```

### Interactive Commands

| Command | Description |
|---------|-------------|
| `s` | Step one instruction |
| `r` | Run until halt |
| `d` | Dump CPU state |
| `m CH SEC` | Show memory at channel/sector |
| `q` | Quit (also works on missiles, we assume) |

## A Brief History

In 1958, the US Air Force needed a guidance computer for their new intercontinental ballistic missile. They contracted Autonetics (a division of North American Aviation) to build it.

The result was the D17B: a 28-kilogram marvel of early 1960s engineering featuring:
- Discrete transistor logic (no integrated circuits)
- A rotating magnetic disc for memory
- The ability to guide a nuclear warhead to within a few hundred metres of its target from 10,000 kilometres away

In 1962, the first Minuteman missiles went on alert. The D17B worked. Rather well, actually.

The D37C followed in 1965, using newfangled integrated circuits to halve the weight and double the memory. It also added hardware division, which the missile engineers presumably celebrated with restrained enthusiasm.

**Sixty years later, the D37C is still in service.** The current Minuteman III missiles use guidance computers that are, architecturally, the same machines. They've been refurbished, certainly. But the instruction set? The 24-bit words? The rotating disc memory concept? Still there.

The US Air Force has been trying to replace them since the 1990s. It's... not gone well.

## Why Does This Exist?

Honestly? I was building language servers for obsolete programming languages (JOVIAL, CMS-2, HAL/S, CORAL 66) and got a bit carried away.

Also, I found it genuinely remarkable that 400 nuclear weapons are controlled by a computer architecture that predates the Apollo Guidance Computer. Someone should probably document how it works before everyone who remembers forgets.

## Documentation Sources

- **D17B Computer Programming Manual** (Sep 1971) - DTIC
- **D17B Programming Manual Supplement** (Dec 1972) - Tulane MCUG
- **D37C Conversion for General Purpose Applications** (Mar 1974) - AFIT Thesis

All sourced from publicly available archives. We're not entirely mad.

## Accuracy

This emulator is believed to be reasonably accurate but has not been validated against actual missile hardware. If you're planning to use this to guide nuclear weapons, please reconsider your life choices.

## Licence

Copyright 2025 Zane Hambly

Licensed under the Apache Licence, Version 2.0. See [LICENSE](LICENSE) for details.

Not that the original engineers bothered with such things. It was 1962. Copyright was for books.

## Related Projects

If building an emulator for nuclear missile guidance computers has left you wanting more vintage military computing, might I suggest:

- **[JOVIAL J73 LSP](https://github.com/Zaneham/jovial-lsp)** - Language server for the programming language that flies F-15s, B-52s, and AWACS. Same era as the D17B, considerably less plutonium.

- **[CMS-2 LSP](https://github.com/Zaneham/cms2-lsp)** - Language server for the US Navy's tactical language. Powers Aegis cruisers, which are meant to intercept things the Minuteman might be sending at them. Full circle.

- **[CORAL 66 LSP](https://github.com/Zaneham/coral66-lsp)** - The British Ministry of Defence's real-time language. Developed at the Royal Radar Establishment whilst, one imagines, maintaining a stiff upper lip about the whole nuclear deterrence situation. Powers Tornado aircraft. Features Crown Copyright.

- **[HAL/S LSP](https://github.com/Zaneham/hals-lsp)** - NASA's Space Shuttle language. For when you want to go to space in a more peaceful, reusable manner. Comes back down gently. Usually.

## See Also

- [Hackaday: The Computers of Voyager](https://hackaday.com/2024/05/13/the-computers-of-voyager/) - Similar vintage, better PR
- [DTIC](https://apps.dtic.mil/) - Where cold war documentation goes to be declassified
- Your nearest missile silo (please do not actually visit)

---

*"The Minuteman weapon system is expected to be operational until at least 2030."*
— Every US Air Force statement since 1995

