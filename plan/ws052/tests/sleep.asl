/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The table of the WS052 p003 host test (run-host-sleep.sh): an LPS0
 * device with both _DSM families, devices with _PR0, _PR3, _PS0, _PS3,
 * _PRW, _DSW, _PSW and _S0W, and three GPE methods.  Each method appends
 * a byte to a trace the test reads back: SEQ for the LPS0 calls (0x0n
 * Intel's function n, 0x1n Microsoft's), TRAC for everything else.
 */
DefinitionBlock ("", "DSDT", 2, "ZEDBSD", "WS052P03", 1)
{
	Name (SEQ, Zero)
	Name (TRAC, Zero)

	/* Appends one byte to the trace. */
	Method (LOG, 1, Serialized)
	{
		TRAC = ((TRAC << 8) | Arg0)
	}

	/* Reads the trace and starts it again. */
	Method (TAKE, 0, Serialized)
	{
		Local0 = TRAC
		TRAC = Zero
		Return (Local0)
	}

	/* Reads the LPS0 sequence and starts it again. */
	Method (TAKS, 0, Serialized)
	{
		Local0 = SEQ
		SEQ = Zero
		Return (Local0)
	}

	Scope (\_SB)
	{
		Device (PEPD)
		{
			Name (_HID, "INT33A1")
			Name (_CID, EisaId ("PNP0D80"))
			Method (_STA, 0, NotSerialized)
			{
				Return (0x0F)
			}

			Method (_DSM, 4, Serialized)
			{
				If ((Arg0 == ToUUID ("c4eb40a0-6cd2-11e2-bcfd-0800200c9a66")))
				{
					If ((Arg2 == Zero))
					{
						Return (Buffer (One) { 0x7F })
					}

					SEQ = ((SEQ << 8) | Arg2)
					Return (Zero)
				}

				If ((Arg0 == ToUUID ("11e00d56-ce64-47ce-837b-1f898f9aa461")))
				{
					If ((Arg1 != Zero))
					{
						Return (Buffer (One) { 0x00 })
					}

					/* Functions 0 and 3 to 8, as Microsoft's are. */
					If ((Arg2 == Zero))
					{
						Return (Buffer (0x02) { 0xF9, 0x01 })
					}

					SEQ = ((SEQ << 8) | (0x10 | Arg2))
					Return (Zero)
				}

				Return (Buffer (One) { 0x00 })
			}
		}

		PowerResource (PRA, 0x00, 0x0000)
		{
			Name (STAT, Zero)
			Method (_STA, 0, NotSerialized)
			{
				Return (STAT)
			}

			Method (_ON, 0, NotSerialized)
			{
				STAT = One
				\LOG (0xA1)
			}

			Method (_OFF, 0, NotSerialized)
			{
				STAT = Zero
				\LOG (0xA0)
			}
		}

		PowerResource (PRB, 0x00, 0x0000)
		{
			Name (STAT, Zero)
			Method (_STA, 0, NotSerialized)
			{
				Return (STAT)
			}

			Method (_ON, 0, NotSerialized)
			{
				STAT = One
				\LOG (0xB1)
			}

			Method (_OFF, 0, NotSerialized)
			{
				STAT = Zero
				\LOG (0xB0)
			}
		}

		PowerResource (PRW1, 0x00, 0x0000)
		{
			Name (STAT, Zero)
			Method (_STA, 0, NotSerialized)
			{
				Return (STAT)
			}

			Method (_ON, 0, NotSerialized)
			{
				STAT = One
				\LOG (0xC1)
			}

			Method (_OFF, 0, NotSerialized)
			{
				STAT = Zero
				\LOG (0xC0)
			}
		}

		/* D0 needs PRA and PRB, D3hot PRB; wakes through GPE 0x12 with PRW1 and _DSW. */
		Device (DEV0)
		{
			Name (_ADR, Zero)
			Name (_PR0, Package (0x02) { PRA, PRB })
			Name (_PR3, Package (0x01) { PRB })
			Method (_PS0, 0, NotSerialized)
			{
				\LOG (0xD0)
			}

			Method (_PS3, 0, NotSerialized)
			{
				\LOG (0xD3)
			}

			Name (_PRW, Package (0x03) { 0x12, 0x03, PRW1 })
			Method (_DSW, 3, NotSerialized)
			{
				\LOG ((0xE0 | ((Arg0 << 3) | Arg2)))
			}

			Method (_S0W, 0, NotSerialized)
			{
				Return (0x03)
			}
		}

		/* Shares PRB, has no _PSx, wakes through GPE 0x13 (no method) with _PSW. */
		Device (DEV1)
		{
			Name (_ADR, One)
			Name (_PR0, Package (0x01) { PRB })
			Name (_PRW, Package (0x02) { 0x13, 0x03 })
			Method (_PSW, 1, NotSerialized)
			{
				\LOG ((0xF0 | Arg0))
			}
		}

		/* Has neither _PS1 nor _PR1, so D1 is refused; no _PRW and no _S0W. */
		Device (DEV2)
		{
			Name (_ADR, 0x02)
		}

		/* A _PRW whose GPE is in a GPE block device, which the helpers refuse. */
		Device (DEV3)
		{
			Name (_ADR, 0x03)
			Name (_PRW, Package (0x02) { Package (0x02) { \_SB.DEV2, Zero }, 0x03 })
		}
	}

	Scope (\_GPE)
	{
		/* DEV0's wake. */
		Method (_L12, 0, NotSerialized)
		{
			\LOG (0x12)
		}

		/* A runtime GPE, masked while the system sleeps. */
		Method (_L14, 0, NotSerialized)
		{
			\LOG (0x14)
		}
	}
}
