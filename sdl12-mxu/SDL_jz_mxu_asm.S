/*
 * Ingenic JZ4760B MXU SIMD & MIPS32 cache-optimized kernels for RetroFW 2.4 SDL 1.2.
 *
 * All MXU .word encodings are copied verbatim from hardware-verified
 * soundux_mxu.S (0/1024 diffs on real JZ4760B) and are only invoked after
 * /proc/cpuinfo confirms kernel MXU context-switch support.
 */
	.text
	.set	noreorder
	.set	noat

/* void sdl_jz_mxu_enable(void) */
	.globl	sdl_jz_mxu_enable
	.type	sdl_jz_mxu_enable, @function
sdl_jz_mxu_enable:
	li	$2, 1
	.word	0x7002042f		/* S32I2M xr16, $2 (enable MXU) */
	nop				/* >=3-cycle JZ4760B XBurst hazard */
	nop
	nop
	nop
	jr	$31
	nop

/* void sdl_mips_memset4_cached(Uint32 *dst, Uint32 val, int count)
 * Fast 32-byte cache-line fill using MIPS pref 30 (PrepareForStore) for
 * cached software surfaces (SDL_SWSURFACE). Never issues pref 30 on
 * unaligned addresses or partial tail cache lines.
 */
	.globl	sdl_mips_memset4_cached
	.type	sdl_mips_memset4_cached, @function
sdl_mips_memset4_cached:
	blez	$6, 4f
	nop
	/* Align dst up to 32-byte cache-line boundary */
1:
	andi	$8, $4, 31
	beqz	$8, 2f
	nop
	sw	$5, 0($4)
	addiu	$6, $6, -1
	bgtz	$6, 1b
	addiu	$4, $4, 4
	jr	$31
	nop
2:
	slti	$8, $6, 8
	bnez	$8, 3f
	nop
	/* Main 32-byte PrepareForStore loop */
20:
	pref	30, 0($4)		/* PrepareForStore: skip DDR read-allocate */
	sw	$5, 0($4)
	sw	$5, 4($4)
	sw	$5, 8($4)
	sw	$5, 12($4)
	sw	$5, 16($4)
	sw	$5, 20($4)
	sw	$5, 24($4)
	sw	$5, 28($4)
	addiu	$6, $6, -8
	slti	$8, $6, 8
	beqz	$8, 20b
	addiu	$4, $4, 32
3:
	blez	$6, 4f
	nop
30:
	sw	$5, 0($4)
	addiu	$6, $6, -1
	bgtz	$6, 30b
	addiu	$4, $4, 4
4:
	jr	$31
	nop

/* void sdl_mips_memset4_uncached(Uint32 *dst, Uint32 val, int count)
 * Unrolled 32-byte store burst without pref 30 (safe for uncached /dev/fb0).
 */
	.globl	sdl_mips_memset4_uncached
	.type	sdl_mips_memset4_uncached, @function
sdl_mips_memset4_uncached:
	blez	$6, 4f
	nop
	slti	$8, $6, 8
	bnez	$8, 3f
	nop
20:
	sw	$5, 0($4)
	sw	$5, 4($4)
	sw	$5, 8($4)
	sw	$5, 12($4)
	sw	$5, 16($4)
	sw	$5, 20($4)
	sw	$5, 24($4)
	sw	$5, 28($4)
	addiu	$6, $6, -8
	slti	$8, $6, 8
	beqz	$8, 20b
	addiu	$4, $4, 32
3:
	blez	$6, 4f
	nop
30:
	sw	$5, 0($4)
	addiu	$6, $6, -1
	bgtz	$6, 30b
	addiu	$4, $4, 4
4:
	jr	$31
	nop

/* void sdl_mxu_memcpy32_cached(void *dst, const void *src, unsigned int nbytes)
 * 4-byte-aligned cached memory copy using pref 0 (src) + pref 30 (32B aligned dst)
 * + MXU xr1..xr8 32-byte burst loads/stores.
 */
	.globl	sdl_mxu_memcpy32_cached
	.type	sdl_mxu_memcpy32_cached, @function
sdl_mxu_memcpy32_cached:
	beqz	$6, 5f
	nop
	/* Head-align dst ($4) to 32-byte cache-line boundary (4 bytes at a time) */
1:
	andi	$8, $4, 31
	beqz	$8, 2f
	sltiu	$9, $6, 4
	bnez	$9, 4f
	nop
	lw	$10, 0($5)
	addiu	$5, $5, 4
	sw	$10, 0($4)
	addiu	$6, $6, -4
	b	1b
	addiu	$4, $4, 4
2:
	sltiu	$8, $6, 32
	bnez	$8, 3f
	nop
20:
	pref	0, 64($5)		/* prefetch src 2 cache lines ahead */
	.word	0x70a00050		/* S32LDD xr1, $5, 0 */
	.word	0x70a00490		/* S32LDD xr2, $5, 4 */
	.word	0x70a008d0		/* S32LDD xr3, $5, 8 */
	.word	0x70a00d10		/* S32LDD xr4, $5, 12 */
	.word	0x70a01150		/* S32LDD xr5, $5, 16 */
	.word	0x70a01590		/* S32LDD xr6, $5, 20 */
	.word	0x70a019d0		/* S32LDD xr7, $5, 24 */
	.word	0x70a01e10		/* S32LDD xr8, $5, 28 */
	pref	30, 0($4)		/* PrepareForStore on 32B aligned dst */
	addiu	$6, $6, -32
	addiu	$5, $5, 32
	.word	0x70800051		/* S32STD xr1, $4, 0 */
	.word	0x70800491		/* S32STD xr2, $4, 4 */
	.word	0x708008d1		/* S32STD xr3, $4, 8 */
	.word	0x70800d11		/* S32STD xr4, $4, 12 */
	.word	0x70801151		/* S32STD xr5, $4, 16 */
	.word	0x70801591		/* S32STD xr6, $4, 20 */
	.word	0x708019d1		/* S32STD xr7, $4, 24 */
	.word	0x70801e11		/* S32STD xr8, $4, 28 */
	sltiu	$8, $6, 32
	beqz	$8, 20b
	addiu	$4, $4, 32
3:
	sltiu	$8, $6, 4
	bnez	$8, 4f
	nop
30:
	lw	$10, 0($5)
	addiu	$5, $5, 4
	sw	$10, 0($4)
	addiu	$6, $6, -4
	sltiu	$8, $6, 4
	beqz	$8, 30b
	addiu	$4, $4, 4
4:
	beqz	$6, 5f
	nop
40:
	lbu	$10, 0($5)
	addiu	$5, $5, 1
	sb	$10, 0($4)
	addiu	$6, $6, -1
	bnez	$6, 40b
	addiu	$4, $4, 1
5:
	jr	$31
	nop

/* void sdl_mxu_mix_s16_lsb(Sint16 *dst, const Sint16 *src, int sample_count, int volume)
 * Bit-exact AUDIO_S16LSB mixer using JZ4760B MXU branchless saturation
 * (S32MIN/S32MAX/S32SFL/S32SDI).
 * Caller guarantees dst is 4-byte aligned and sample_count >= 2.
 * Note: internally moves dst_rd -> $2, dst_wr (dst-4) -> $5, src -> $4.
 */
	.globl	sdl_mxu_mix_s16_lsb
	.type	sdl_mxu_mix_s16_lsb, @function
sdl_mxu_mix_s16_lsb:
	blez	$6, 9f
	nop
	move	$2, $4			/* $2 = dst read ptr */
	move	$4, $5			/* $4 = src read ptr */
	addiu	$5, $2, -4		/* $5 = dst write ptr - 4 (for S32SDI +4) */
	li	$8, 32767
	li	$9, -32768
	.word	0x700803af		/* S32I2M xr14, $8 (32767) */
	.word	0x700903ef		/* S32I2M xr15, $9 (-32768) */

	slti	$8, $6, 4
	bnez	$8, 2f
	nop
1:
	lh	$10, 0($4)
	lh	$11, 2($4)
	lh	$12, 4($4)
	lh	$13, 6($4)
	mul	$10, $10, $7
	mul	$11, $11, $7
	mul	$12, $12, $7
	mul	$13, $13, $7
	/* Exact C signed division by 128: (x + ((x >> 31) >>> 25)) >> 7 */
	sra	$8, $10, 31
	sra	$9, $11, 31
	srl	$8, $8, 25
	srl	$9, $9, 25
	addu	$10, $10, $8
	addu	$11, $11, $9
	sra	$10, $10, 7
	sra	$11, $11, 7

	sra	$8, $12, 31
	sra	$9, $13, 31
	srl	$8, $8, 25
	srl	$9, $9, 25
	addu	$12, $12, $8
	addu	$13, $13, $9
	sra	$12, $12, 7
	sra	$13, $13, 7

	lh	$8, 0($2)
	lh	$9, 2($2)
	lh	$14, 4($2)
	lh	$15, 6($2)
	addu	$10, $10, $8
	addu	$11, $11, $9
	addu	$12, $12, $14
	addu	$13, $13, $15

	.word	0x700a006f		/* S32I2M xr1, $10 */
	.word	0x700b00af		/* S32I2M xr2, $11 */
	.word	0x700c016f		/* S32I2M xr5, $12 */
	.word	0x700d01af		/* S32I2M xr6, $13 */
	addiu	$6, $6, -4
	.word	0x70078443		/* S32MIN xr1, xr1, xr14 */
	.word	0x70078883		/* S32MIN xr2, xr2, xr14 */
	.word	0x70079543		/* S32MIN xr5, xr5, xr14 */
	.word	0x70079983		/* S32MIN xr6, xr6, xr14 */
	.word	0x7003c443		/* S32MAX xr1, xr1, xr15 */
	.word	0x7003c883		/* S32MAX xr2, xr2, xr15 */
	.word	0x7003d543		/* S32MAX xr5, xr5, xr15 */
	.word	0x7003d983		/* S32MAX xr6, xr6, xr15 */
	.word	0x731048fd		/* S32SFL xr3, xr2, xr1, xr4, ptn3 */
	.word	0x732159fd		/* S32SFL xr7, xr6, xr5, xr8, ptn3 */
	.word	0x70a00515		/* S32SDI xr4, $5, 4 */
	.word	0x70a00615		/* S32SDI xr8, $5, 4 */
	addiu	$2, $2, 8
	slti	$8, $6, 4
	beqz	$8, 1b
	addiu	$4, $4, 8
2:
	slti	$8, $6, 2
	bnez	$8, 3f
	nop
	lh	$10, 0($4)
	lh	$11, 2($4)
	mul	$10, $10, $7
	mul	$11, $11, $7
	sra	$8, $10, 31
	sra	$9, $11, 31
	srl	$8, $8, 25
	srl	$9, $9, 25
	addu	$10, $10, $8
	addu	$11, $11, $9
	sra	$10, $10, 7
	sra	$11, $11, 7
	lh	$8, 0($2)
	lh	$9, 2($2)
	addu	$10, $10, $8
	addu	$11, $11, $9
	.word	0x700a006f		/* S32I2M xr1, $10 */
	.word	0x700b00af		/* S32I2M xr2, $11 */
	.word	0x70078443		/* S32MIN xr1, xr1, xr14 */
	.word	0x70078883		/* S32MIN xr2, xr2, xr14 */
	.word	0x7003c443		/* S32MAX xr1, xr1, xr15 */
	.word	0x7003c883		/* S32MAX xr2, xr2, xr15 */
	.word	0x731048fd		/* S32SFL xr3, xr2, xr1, xr4, ptn3 */
	.word	0x70a00515		/* S32SDI xr4, $5, 4 */
	addiu	$2, $2, 4
	addiu	$4, $4, 4
	addiu	$6, $6, -2
3:
	blez	$6, 9f
	nop
	/* Final odd sample scalar */
	lh	$10, 0($4)
	mul	$10, $10, $7
	sra	$8, $10, 31
	srl	$8, $8, 25
	addu	$10, $10, $8
	sra	$10, $10, 7
	lh	$8, 0($2)
	addu	$10, $10, $8
	li	$8, 32767
	slt	$9, $8, $10
	movn	$10, $8, $9
	li	$8, -32768
	slt	$9, $10, $8
	movn	$10, $8, $9
	sh	$10, 0($2)
9:
	jr	$31
	nop
