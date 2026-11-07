/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

use std::mem::MaybeUninit;
use std::sync::OnceLock;

static STRAIGHT_TABLE: OnceLock<Box<[u8; 65536]>> = OnceLock::new();

pub fn straight_rgba_from_premultiplied_bgra(
    width: u32,
    height: u32,
    stride: usize,
    bgra: &[u8],
) -> Vec<u8> {
    let row_bytes = width as usize * 4;
    let chunk = stride.max(row_bytes);
    let rows = if bgra.is_empty() {
        0
    } else {
        bgra.len().div_ceil(chunk).min(height as usize)
    };
    if rows == 0 {
        return Vec::new();
    }

    let length = row_bytes * rows;
    let mut rgba: Vec<u8> = Vec::with_capacity(length);
    let table = straight_table();
    let rows = bgra
        .chunks(chunk)
        .take(rows)
        .zip(rgba.spare_capacity_mut().chunks_mut(row_bytes));
    for (row, out) in rows {
        let src = &row[..row_bytes];
        if !swizzle_opaque_rows(out, src) {
            straight_row(out, src, table);
        }
    }
    unsafe { rgba.set_len(length) };
    rgba
}

fn straight_table() -> &'static [u8; 65536] {
    STRAIGHT_TABLE.get_or_init(|| {
        let mut table = Box::new([0u8; 65536]);
        for alpha in 1u32..=255 {
            for channel in 0u32..=255 {
                table[(alpha * 256 + channel) as usize] =
                    ((channel * 255 + alpha / 2) / alpha).min(255) as u8;
            }
        }
        table
    })
}

fn swizzle_word(word: u32) -> u32 {
    (word.rotate_left(16) & 0x00FF_00FF) | (word & 0xFF00_FF00)
}

fn write_bytes(dst: &mut [MaybeUninit<u8>], bytes: &[u8]) {
    assert_eq!(dst.len(), bytes.len());
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), dst.as_mut_ptr().cast::<u8>(), bytes.len());
    }
}

fn swizzle_opaque_rows(dst: &mut [MaybeUninit<u8>], src: &[u8]) -> bool {
    const BLUE: u128 = 0x0000_00FF_0000_00FF_0000_00FF_0000_00FF;
    const RED: u128 = 0x00FF_0000_00FF_0000_00FF_0000_00FF_0000;
    const KEPT: u128 = 0xFF00_FF00_FF00_FF00_FF00_FF00_FF00_FF00;
    const ALPHA: u128 = 0xFF00_0000_FF00_0000_FF00_0000_FF00_0000;
    let (dst_quads, dst_tail) = dst.as_chunks_mut::<16>();
    let (src_quads, src_tail) = src.as_chunks::<16>();
    for (dst, src) in dst_quads.iter_mut().zip(src_quads) {
        let quad = u128::from_le_bytes(*src);
        if quad & ALPHA != ALPHA {
            return false;
        }
        let swizzled = ((quad & BLUE) << 16) | ((quad & RED) >> 16) | (quad & KEPT);
        write_bytes(dst, &swizzled.to_le_bytes());
    }
    for (dst, src) in dst_tail
        .as_chunks_mut::<4>()
        .0
        .iter_mut()
        .zip(src_tail.as_chunks::<4>().0)
    {
        if src[3] != 255 {
            return false;
        }
        write_bytes(dst, &swizzle_word(u32::from_le_bytes(*src)).to_le_bytes());
    }
    true
}

fn straight_row(dst: &mut [MaybeUninit<u8>], src: &[u8], table: &[u8; 65536]) {
    for (dst, src) in dst
        .as_chunks_mut::<4>()
        .0
        .iter_mut()
        .zip(src.as_chunks::<4>().0)
    {
        let alpha = usize::from(src[3]);
        if alpha == 255 {
            write_bytes(dst, &swizzle_word(u32::from_le_bytes(*src)).to_le_bytes());
        } else {
            let base = alpha * 256;
            write_bytes(
                dst,
                &[
                    table[base + usize::from(src[2])],
                    table[base + usize::from(src[1])],
                    table[base + usize::from(src[0])],
                    src[3],
                ],
            );
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn expected(pixel: &[u8; 4]) -> [u8; 4] {
        let alpha = u32::from(pixel[3]);
        let straight = |channel: u8| {
            (u32::from(channel) * 255 + alpha / 2)
                .checked_div(alpha)
                .unwrap_or(0)
                .min(255) as u8
        };
        [
            straight(pixel[2]),
            straight(pixel[1]),
            straight(pixel[0]),
            pixel[3],
        ]
    }

    #[test]
    fn opaque_pixels_swap_blue_and_red() {
        let bgra = [10, 20, 30, 255, 40, 50, 60, 255];
        assert_eq!(
            straight_rgba_from_premultiplied_bgra(2, 1, 8, &bgra),
            vec![30, 20, 10, 255, 60, 50, 40, 255]
        );
    }

    #[test]
    fn rows_skip_the_padding_past_the_width() {
        let bgra = [
            1, 2, 3, 255, 0xee, 0xee, 0xee, 0xee, 4, 5, 6, 255, 0xee, 0xee, 0xee, 0xee,
        ];
        assert_eq!(
            straight_rgba_from_premultiplied_bgra(1, 2, 8, &bgra),
            vec![3, 2, 1, 255, 6, 5, 4, 255]
        );
    }

    #[test]
    fn the_last_row_may_end_at_its_width() {
        let bgra = [1, 2, 3, 255, 0, 0, 0, 0, 4, 5, 6, 255];
        assert_eq!(
            straight_rgba_from_premultiplied_bgra(1, 2, 8, &bgra),
            vec![3, 2, 1, 255, 6, 5, 4, 255]
        );
    }

    #[test]
    fn translucent_pixels_are_unpremultiplied() {
        let bgra = [0, 64, 128, 128];
        assert_eq!(
            straight_rgba_from_premultiplied_bgra(1, 1, 4, &bgra),
            vec![255, 128, 0, 128]
        );
    }

    #[test]
    fn transparent_pixels_become_transparent_black() {
        let bgra = [9, 9, 9, 0];
        assert_eq!(
            straight_rgba_from_premultiplied_bgra(1, 1, 4, &bgra),
            vec![0, 0, 0, 0]
        );
    }

    #[test]
    fn a_translucent_tail_after_opaque_pixels_keeps_the_row_exact() {
        let bgra = [1, 2, 3, 255, 10, 20, 30, 255, 4, 8, 16, 64];
        assert_eq!(
            straight_rgba_from_premultiplied_bgra(3, 1, 12, &bgra),
            vec![3, 2, 1, 255, 30, 20, 10, 255, 64, 32, 16, 64]
        );
    }

    #[test]
    fn a_translucent_pixel_after_a_whole_opaque_quad_still_takes_the_table() {
        let mut bgra = [0u8; 8 * 4];
        for (index, pixel) in bgra.as_chunks_mut::<4>().0.iter_mut().enumerate() {
            *pixel = [index as u8, index as u8 + 1, index as u8 + 2, 255];
        }
        bgra[7 * 4..].copy_from_slice(&[4, 8, 16, 64]);
        let rgba = straight_rgba_from_premultiplied_bgra(8, 1, 32, &bgra);
        for (index, pixel) in bgra.as_chunks::<4>().0.iter().enumerate() {
            assert_eq!(&rgba[index * 4..index * 4 + 4], expected(pixel));
        }
    }

    #[test]
    fn the_table_matches_the_dividing_rule_for_every_pair() {
        let table = straight_table();
        for alpha in 1u32..=255 {
            for channel in 0u32..=255 {
                let expected = ((channel * 255 + alpha / 2) / alpha).min(255) as u8;
                assert_eq!(table[(alpha * 256 + channel) as usize], expected);
            }
        }
        assert_eq!(table[0], 0);
        assert_eq!(table[255], 0);
    }

    #[test]
    fn every_row_width_takes_the_same_path_as_the_formula() {
        for width in 1u32..=17 {
            let height = 3;
            let stride = width as usize * 4 + 7;
            let mut bgra = vec![0u8; stride * height as usize];
            for (index, byte) in bgra.iter_mut().enumerate() {
                *byte = (index * 37 + 11) as u8;
            }
            if width > 1 {
                bgra[3] = 255;
            }
            let rgba = straight_rgba_from_premultiplied_bgra(width, height, stride, &bgra);
            for row in 0..height as usize {
                for column in 0..width as usize {
                    let start = row * stride + column * 4;
                    let pixel: &[u8; 4] = bgra[start..start + 4].try_into().unwrap();
                    let at = row * width as usize * 4 + column * 4;
                    assert_eq!(
                        &rgba[at..at + 4],
                        expected(pixel),
                        "width {width}, row {row}, column {column}"
                    );
                }
            }
        }
    }
}
