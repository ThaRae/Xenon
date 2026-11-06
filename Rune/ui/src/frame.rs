/*
 * Copyright (c) 2026, the Xenon developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

pub fn straight_rgba_from_premultiplied_bgra(
    width: u32,
    height: u32,
    stride: usize,
    bgra: &[u8],
) -> Vec<u8> {
    let (width, height) = (width as usize, height as usize);
    let row_bytes = width * 4;
    let mut rgba = Vec::with_capacity(row_bytes * height);
    for row in bgra.chunks(stride.max(row_bytes)).take(height) {
        for &[b, g, r, a] in row[..row_bytes].as_chunks::<4>().0 {
            rgba.extend_from_slice(&unpremultiply([r, g, b, a]));
        }
    }
    rgba
}

fn unpremultiply([r, g, b, a]: [u8; 4]) -> [u8; 4] {
    match a {
        255 => [r, g, b, 255],
        0 => [0, 0, 0, 0],
        _ => {
            let straight =
                |c: u8| ((u32::from(c) * 255 + u32::from(a) / 2) / u32::from(a)).min(255) as u8;
            [straight(r), straight(g), straight(b), a]
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
}
