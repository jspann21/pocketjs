//! `.pocket` package reader — the device side of the container format.
//! contracts/spec/pocket-package.ts is the format authority; both implementations are
//! pinned to the SAME committed fixture (tests/fixtures/packages/), so they
//! cannot drift apart silently.
//!
//! Zero-copy by design: every accessor returns borrowed slices of the input
//! bytes. An EBOOT embeds `.pocket` files verbatim in .rodata and boots a
//! guest straight out of them — the js section carries its QuickJS NUL
//! terminator (eval with len - 1), the pak section feeds pak::feed as-is.

use core::str;

pub const MAGIC: u32 = 0x544b_4350; // "PCKT"
pub const VERSION: u32 = 1;
const HEADER_SIZE: usize = 16;
const VARIANT_SIZE: usize = 40;
const SECTION_SIZE: usize = 16;
const TARGET_BYTES: usize = 16;
const ALIGN: usize = 16;

/// Section kinds (append-only; skip what you do not know).
pub mod section {
    pub const IDENTITY: u32 = 1;
    pub const PLAN: u32 = 2;
    pub const JS: u32 = 3;
    pub const PAK: u32 = 4;
    pub const COVER: u32 = 5;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PackageError {
    Truncated,
    BadMagic,
    BadVersion,
    HashMismatch,
    BadUtf8,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GuestError {
    Package(PackageError),
    MissingVariant,
    HostAbiMismatch,
    MissingIdentity,
    MissingPlan,
    MissingJavaScript,
    JavaScriptNotTerminated,
}

impl From<PackageError> for GuestError {
    fn from(value: PackageError) -> Self {
        Self::Package(value)
    }
}

#[derive(Debug)]
pub struct Package<'a> {
    bytes: &'a [u8],
    manifest_len: usize,
    variant_count: usize,
    table_off: usize,
}

#[derive(Clone, Copy)]
pub struct Variant<'a> {
    bytes: &'a [u8],
    pub target: &'a str,
    pub host_abi: u32,
    pub variant_hash: u64,
    section_count: usize,
    sections_off: usize,
}

pub struct Identity<'a> {
    pub output: &'a str,
    pub id: &'a str,
    pub title: &'a str,
}

/// A filesystem package admitted for one native host. The package allocation
/// must outlive these slices; native runtimes keep it until guest teardown.
pub struct Guest<'a> {
    pub js: &'a [u8],
    pub pak: &'a [u8],
    pub plan: &'a [u8],
    pub package_hash: u64,
    pub variant_hash: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlanError {
    Syntax,
    DuplicateField,
    MissingTarget,
    MissingViewport,
    MissingFeatures,
    TargetMismatch,
    HostAbiMismatch,
    ViewportMismatch,
    UnsupportedFeature,
}

pub struct FixedPlanContract<'a> {
    pub target: &'a str,
    pub host_abi: u32,
    pub width: u32,
    pub height: u32,
    pub presentation: &'a str,
    pub raster_density: u32,
    pub supported_features: &'a [&'a str],
}

struct JsonCursor<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> JsonCursor<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, offset: 0 }
    }

    fn whitespace(&mut self) {
        while matches!(self.bytes.get(self.offset), Some(b' ' | b'\n' | b'\r' | b'\t')) {
            self.offset += 1;
        }
    }

    fn consume(&mut self, expected: u8) -> bool {
        self.whitespace();
        if self.bytes.get(self.offset) == Some(&expected) {
            self.offset += 1;
            true
        } else {
            false
        }
    }

    fn expect(&mut self, expected: u8) -> Result<(), PlanError> {
        self.consume(expected).then_some(()).ok_or(PlanError::Syntax)
    }

    fn comma_before_next(&mut self, closing: u8) -> Result<(), PlanError> {
        self.expect(b',')?;
        self.whitespace();
        if self.bytes.get(self.offset) == Some(&closing) {
            Err(PlanError::Syntax)
        } else {
            Ok(())
        }
    }

    fn string(&mut self) -> Result<&'a [u8], PlanError> {
        self.whitespace();
        if self.bytes.get(self.offset) != Some(&b'"') {
            return Err(PlanError::Syntax);
        }
        self.offset += 1;
        let start = self.offset;
        while let Some(&byte) = self.bytes.get(self.offset) {
            match byte {
                b'"' => {
                    let result = &self.bytes[start..self.offset];
                    self.offset += 1;
                    return Ok(result);
                }
                b'\\' | 0x00..=0x1f => return Err(PlanError::Syntax),
                _ => self.offset += 1,
            }
        }
        Err(PlanError::Syntax)
    }

    fn literal(&mut self, value: &[u8]) -> bool {
        self.whitespace();
        if self.bytes.get(self.offset..self.offset + value.len()) == Some(value) {
            self.offset += value.len();
            true
        } else {
            false
        }
    }

    fn u32(&mut self) -> Result<u32, PlanError> {
        self.whitespace();
        let start = self.offset;
        let mut value = 0u32;
        while let Some(byte @ b'0'..=b'9') = self.bytes.get(self.offset).copied() {
            value = value.checked_mul(10)
                .and_then(|v| v.checked_add((byte - b'0') as u32))
                .ok_or(PlanError::Syntax)?;
            self.offset += 1;
        }
        if self.offset == start { Err(PlanError::Syntax) } else { Ok(value) }
    }

    fn pair(&mut self) -> Result<(u32, u32), PlanError> {
        self.expect(b'[')?;
        let first = self.u32()?;
        self.expect(b',')?;
        let second = self.u32()?;
        self.expect(b']')?;
        Ok((first, second))
    }

    fn skip_string(&mut self) -> Result<(), PlanError> {
        self.whitespace();
        if self.bytes.get(self.offset) != Some(&b'"') {
            return Err(PlanError::Syntax);
        }
        self.offset += 1;
        while let Some(&byte) = self.bytes.get(self.offset) {
            self.offset += 1;
            match byte {
                b'"' => return Ok(()),
                b'\\' => {
                    let escaped = *self.bytes.get(self.offset).ok_or(PlanError::Syntax)?;
                    self.offset += 1;
                    if escaped == b'u' {
                        for _ in 0..4 {
                            let digit = *self.bytes.get(self.offset).ok_or(PlanError::Syntax)?;
                            if !digit.is_ascii_hexdigit() { return Err(PlanError::Syntax); }
                            self.offset += 1;
                        }
                    } else if !matches!(escaped, b'"' | b'\\' | b'/' | b'b' | b'f' | b'n' | b'r' | b't') {
                        return Err(PlanError::Syntax);
                    }
                }
                0x00..=0x1f => return Err(PlanError::Syntax),
                _ => {}
            }
        }
        Err(PlanError::Syntax)
    }

    fn skip_number(&mut self) -> Result<(), PlanError> {
        self.whitespace();
        if self.bytes.get(self.offset) == Some(&b'-') {
            self.offset += 1;
        }
        match self.bytes.get(self.offset).copied() {
            Some(b'0') => self.offset += 1,
            Some(b'1'..=b'9') => {
                self.offset += 1;
                while matches!(self.bytes.get(self.offset), Some(b'0'..=b'9')) {
                    self.offset += 1;
                }
            }
            _ => return Err(PlanError::Syntax),
        }
        if self.bytes.get(self.offset) == Some(&b'.') {
            self.offset += 1;
            let start = self.offset;
            while matches!(self.bytes.get(self.offset), Some(b'0'..=b'9')) {
                self.offset += 1;
            }
            if self.offset == start { return Err(PlanError::Syntax); }
        }
        if matches!(self.bytes.get(self.offset), Some(b'e' | b'E')) {
            self.offset += 1;
            if matches!(self.bytes.get(self.offset), Some(b'+' | b'-')) {
                self.offset += 1;
            }
            let start = self.offset;
            while matches!(self.bytes.get(self.offset), Some(b'0'..=b'9')) {
                self.offset += 1;
            }
            if self.offset == start { return Err(PlanError::Syntax); }
        }
        Ok(())
    }

    fn skip_value(&mut self, depth: u32) -> Result<(), PlanError> {
        if depth > 16 { return Err(PlanError::Syntax); }
        self.whitespace();
        match self.bytes.get(self.offset).copied() {
            Some(b'"') => self.skip_string(),
            Some(b'{') => {
                self.offset += 1;
                self.whitespace();
                if self.consume(b'}') { return Ok(()); }
                loop {
                    self.skip_string()?;
                    self.expect(b':')?;
                    self.skip_value(depth + 1)?;
                    if self.consume(b'}') { return Ok(()); }
                    self.comma_before_next(b'}')?;
                }
            }
            Some(b'[') => {
                self.offset += 1;
                self.whitespace();
                if self.consume(b']') { return Ok(()); }
                loop {
                    self.skip_value(depth + 1)?;
                    if self.consume(b']') { return Ok(()); }
                    self.comma_before_next(b']')?;
                }
            }
            Some(b't') if self.literal(b"true") => Ok(()),
            Some(b'f') if self.literal(b"false") => Ok(()),
            Some(b'n') if self.literal(b"null") => Ok(()),
            Some(b'-' | b'0'..=b'9') => self.skip_number(),
            _ => Err(PlanError::Syntax),
        }
    }
}

fn parse_target(cursor: &mut JsonCursor<'_>, contract: &FixedPlanContract<'_>) -> Result<(), PlanError> {
    cursor.expect(b'{')?;
    let mut seen = 0u32;
    loop {
        if cursor.consume(b'}') { break; }
        let key = cursor.string()?;
        cursor.expect(b':')?;
        let bit = match key {
            b"hostAbi" => {
                if cursor.u32()? != contract.host_abi { return Err(PlanError::HostAbiMismatch); }
                1u32
            }
            b"id" => {
                if cursor.string()? != contract.target.as_bytes() { return Err(PlanError::TargetMismatch); }
                2u32
            }
            _ => { cursor.skip_value(1)?; 0u32 }
        };
        if bit != 0 && seen & bit != 0 { return Err(PlanError::DuplicateField); }
        seen |= bit;
        if cursor.consume(b'}') { break; }
        cursor.comma_before_next(b'}')?;
    }
    if seen != 3 { return Err(PlanError::MissingTarget); }
    Ok(())
}

fn parse_viewport(cursor: &mut JsonCursor<'_>, contract: &FixedPlanContract<'_>) -> Result<(), PlanError> {
    cursor.expect(b'{')?;
    let mut seen = 0u32;
    loop {
        if cursor.consume(b'}') { break; }
        let key = cursor.string()?;
        cursor.expect(b':')?;
        let bit = match key {
            b"logical" | b"physical" => {
                if cursor.pair()? != (contract.width, contract.height) {
                    return Err(PlanError::ViewportMismatch);
                }
                if key == b"logical" { 1u32 } else { 2u32 }
            }
            b"policy" => {
                if cursor.string()? != b"fixed" { return Err(PlanError::ViewportMismatch); }
                4u32
            }
            b"presentation" => {
                if cursor.string()? != contract.presentation.as_bytes() {
                    return Err(PlanError::ViewportMismatch);
                }
                8u32
            }
            b"rasterDensity" => {
                if cursor.u32()? != contract.raster_density { return Err(PlanError::ViewportMismatch); }
                16u32
            }
            _ => { cursor.skip_value(1)?; 0u32 }
        };
        if bit != 0 && seen & bit != 0 { return Err(PlanError::DuplicateField); }
        seen |= bit;
        if cursor.consume(b'}') { break; }
        cursor.comma_before_next(b'}')?;
    }
    if seen != 31 { return Err(PlanError::MissingViewport); }
    Ok(())
}

fn parse_features(cursor: &mut JsonCursor<'_>, contract: &FixedPlanContract<'_>) -> Result<(), PlanError> {
    cursor.expect(b'{')?;
    let mut seen = 0u64;
    loop {
        if cursor.consume(b'}') { return Ok(()); }
        let feature = cursor.string()?;
        cursor.expect(b':')?;
        if !cursor.literal(b"true") { return Err(PlanError::UnsupportedFeature); }
        let index = contract.supported_features.iter()
            .position(|item| item.as_bytes() == feature)
            .ok_or(PlanError::UnsupportedFeature)?;
        if index >= 64 { return Err(PlanError::UnsupportedFeature); }
        let bit = 1u64 << index;
        if seen & bit != 0 { return Err(PlanError::DuplicateField); }
        seen |= bit;
        if cursor.consume(b'}') { return Ok(()); }
        cursor.comma_before_next(b'}')?;
    }
}

pub fn validate_fixed_plan(plan: &[u8], contract: &FixedPlanContract<'_>) -> Result<(), PlanError> {
    let mut cursor = JsonCursor::new(plan);
    cursor.expect(b'{')?;
    let mut seen = 0u32;
    loop {
        if cursor.consume(b'}') { break; }
        let key = cursor.string()?;
        cursor.expect(b':')?;
        let bit = match key {
            b"target" => { parse_target(&mut cursor, contract)?; 1u32 }
            b"viewport" => { parse_viewport(&mut cursor, contract)?; 2u32 }
            b"features" => { parse_features(&mut cursor, contract)?; 4u32 }
            _ => { cursor.skip_value(1)?; 0u32 }
        };
        if bit != 0 && seen & bit != 0 { return Err(PlanError::DuplicateField); }
        seen |= bit;
        if cursor.consume(b'}') { break; }
        cursor.comma_before_next(b'}')?;
    }
    cursor.whitespace();
    if cursor.offset != plan.len() { return Err(PlanError::Syntax); }
    if seen & 1 == 0 { return Err(PlanError::MissingTarget); }
    if seen & 2 == 0 { return Err(PlanError::MissingViewport); }
    if seen & 4 == 0 { return Err(PlanError::MissingFeatures); }
    Ok(())
}

fn u32_at(bytes: &[u8], off: usize) -> Result<u32, PackageError> {
    let s = bytes.get(off..off + 4).ok_or(PackageError::Truncated)?;
    Ok(u32::from_le_bytes([s[0], s[1], s[2], s[3]]))
}

fn u64_at(bytes: &[u8], off: usize) -> Result<u64, PackageError> {
    let s = bytes.get(off..off + 8).ok_or(PackageError::Truncated)?;
    Ok(u64::from_le_bytes([s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]]))
}

/// FNV-1a64, lockstep with tools/bundle-hash.ts / hosts/psp/build.rs.
pub fn fnv1a64(chunks: &[&[u8]]) -> u64 {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for chunk in chunks {
        for &b in *chunk {
            h ^= b as u64;
            h = h.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }
    h
}

impl<'a> Package<'a> {
    /// Parse and (unless `skip_hash`) verify the footer hash. Embedded
    /// packages were hashed at build time — boot paths pass `skip_hash =
    /// true` and rely on the EBOOT's own build identity; filesystem loads
    /// (dynamic install) MUST verify.
    pub fn parse(bytes: &'a [u8], skip_hash: bool) -> Result<Self, PackageError> {
        if bytes.len() < HEADER_SIZE + 8 {
            return Err(PackageError::Truncated);
        }
        if u32_at(bytes, 0)? != MAGIC {
            return Err(PackageError::BadMagic);
        }
        if u32_at(bytes, 4)? != VERSION {
            return Err(PackageError::BadVersion);
        }
        if !skip_hash {
            let stored = u64_at(bytes, bytes.len() - 8)?;
            if stored != fnv1a64(&[&bytes[..bytes.len() - 8]]) {
                return Err(PackageError::HashMismatch);
            }
        }
        let manifest_len = u32_at(bytes, 8)? as usize;
        let variant_count = u32_at(bytes, 12)? as usize;
        let table_off = (HEADER_SIZE + manifest_len).div_ceil(ALIGN) * ALIGN;
        if table_off + variant_count * VARIANT_SIZE > bytes.len() {
            return Err(PackageError::Truncated);
        }
        Ok(Package { bytes, manifest_len, variant_count, table_off })
    }

    /// pocket.json bytes, verbatim.
    pub fn manifest(&self) -> &'a [u8] {
        &self.bytes[HEADER_SIZE..HEADER_SIZE + self.manifest_len]
    }

    pub fn variant_count(&self) -> usize {
        self.variant_count
    }

    /// The verified footer value. `parse(..., false)` has already compared it
    /// with the bytes; embedded callers that skip verification use it only as
    /// an artifact identity.
    pub fn package_hash(&self) -> Result<u64, PackageError> {
        u64_at(self.bytes, self.bytes.len() - 8)
    }

    pub fn variant(&self, index: usize) -> Result<Variant<'a>, PackageError> {
        if index >= self.variant_count {
            return Err(PackageError::Truncated);
        }
        let entry = self.table_off + index * VARIANT_SIZE;
        let name = self
            .bytes
            .get(entry..entry + TARGET_BYTES)
            .ok_or(PackageError::Truncated)?;
        let len = name.iter().position(|&b| b == 0).unwrap_or(TARGET_BYTES);
        let target = str::from_utf8(&name[..len]).map_err(|_| PackageError::BadUtf8)?;
        Ok(Variant {
            bytes: self.bytes,
            target,
            host_abi: u32_at(self.bytes, entry + 16)?,
            section_count: u32_at(self.bytes, entry + 20)? as usize,
            sections_off: u32_at(self.bytes, entry + 24)? as usize,
            variant_hash: u64_at(self.bytes, entry + 32)?,
        })
    }

    /// The variant for a target id, if the file carries one.
    pub fn find_variant(&self, target: &str) -> Result<Option<Variant<'a>>, PackageError> {
        for i in 0..self.variant_count {
            let v = self.variant(i)?;
            if v.target == target {
                return Ok(Some(v));
            }
        }
        Ok(None)
    }
}

/// Parse a package and admit its guest payload for an exact target/host ABI.
/// This is the shared boundary filesystem-loading hosts use before exposing
/// any package bytes to QuickJS or the retained UI core.
pub fn select_guest<'a>(
    bytes: &'a [u8],
    target: &str,
    host_abi: u32,
    skip_hash: bool,
) -> Result<Guest<'a>, GuestError> {
    let package = Package::parse(bytes, skip_hash)?;
    let variant = package
        .find_variant(target)?
        .ok_or(GuestError::MissingVariant)?;
    if variant.host_abi != host_abi {
        return Err(GuestError::HostAbiMismatch);
    }
    if variant.identity()?.is_none() {
        return Err(GuestError::MissingIdentity);
    }
    let plan = variant
        .section(section::PLAN)?
        .filter(|value| !value.is_empty())
        .ok_or(GuestError::MissingPlan)?;
    let js = variant
        .section(section::JS)?
        .filter(|value| !value.is_empty())
        .ok_or(GuestError::MissingJavaScript)?;
    if js.last() != Some(&0) {
        return Err(GuestError::JavaScriptNotTerminated);
    }
    let pak = variant.section(section::PAK)?.unwrap_or(&[]);
    Ok(Guest {
        js,
        pak,
        plan,
        package_hash: package.package_hash()?,
        variant_hash: variant.variant_hash,
    })
}

impl<'a> Variant<'a> {
    /// A section payload by kind (unknown kinds are simply never asked for —
    /// forward compatible by construction).
    pub fn section(&self, kind: u32) -> Result<Option<&'a [u8]>, PackageError> {
        for i in 0..self.section_count {
            let entry = self.sections_off + i * SECTION_SIZE;
            if u32_at(self.bytes, entry)? == kind {
                let off = u32_at(self.bytes, entry + 8)? as usize;
                let len = u32_at(self.bytes, entry + 12)? as usize;
                return self
                    .bytes
                    .get(off..off + len)
                    .map(Some)
                    .ok_or(PackageError::Truncated);
            }
        }
        Ok(None)
    }

    /// The device registry line (kind 1) — output, id, title without any
    /// JSON parsing on the console.
    pub fn identity(&self) -> Result<Option<Identity<'a>>, PackageError> {
        let Some(bytes) = self.section(section::IDENTITY)? else {
            return Ok(None);
        };
        let mut off = 0usize;
        let mut fields = [""; 3];
        for slot in fields.iter_mut() {
            let len = bytes
                .get(off..off + 2)
                .map(|s| u16::from_le_bytes([s[0], s[1]]) as usize)
                .ok_or(PackageError::Truncated)?;
            off += 2;
            let raw = bytes.get(off..off + len).ok_or(PackageError::Truncated)?;
            *slot = str::from_utf8(raw).map_err(|_| PackageError::BadUtf8)?;
            off += len;
        }
        Ok(Some(Identity { output: fields[0], id: fields[1], title: fields[2] }))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The SAME committed fixture tests/pocket-package.test.ts byte-compares
    /// against the TS encoder — the cross-implementation contract.
    static FIXTURE: &[u8] = include_bytes!("../../../tests/fixtures/packages/synthetic.pocket");

    #[test]
    fn parses_the_shared_fixture() {
        let pkg = Package::parse(FIXTURE, false).unwrap();
        assert_eq!(pkg.variant_count(), 3);
        let targets: alloc::vec::Vec<&str> =
            (0..3).map(|i| pkg.variant(i).unwrap().target).collect();
        assert_eq!(targets, ["macos-widget", "psp", "vita"]);
        assert!(core::str::from_utf8(pkg.manifest()).unwrap().contains("synthetic"));

        let psp = pkg.find_variant("psp").unwrap().unwrap();
        assert_eq!(psp.host_abi, 1);
        let identity = psp.identity().unwrap().unwrap();
        assert_eq!(identity.output, "synthetic-main");
        assert_eq!(identity.title, "Synthetic");
        // The js section ends in the QuickJS NUL (zero-copy eval rule).
        let js = psp.section(section::JS).unwrap().unwrap();
        assert_eq!(*js.last().unwrap(), 0);
        // Target-flavored sections stay per-variant.
        assert_eq!(psp.section(section::PAK).unwrap().unwrap()[0], 10);
        let vita = pkg.find_variant("vita").unwrap().unwrap();
        assert_eq!(vita.section(section::PAK).unwrap().unwrap()[0], 20);
        let widget = pkg.find_variant("macos-widget").unwrap().unwrap();
        assert_eq!(widget.section(section::PAK).unwrap().unwrap()[0], 30);
        assert_eq!(widget.host_abi, 3);
    }

    #[test]
    fn tamper_trips_the_footer_hash() {
        let mut evil = FIXTURE.to_vec();
        let n = evil.len();
        evil[n - 20] ^= 0xff;
        assert_eq!(Package::parse(&evil, false).unwrap_err(), PackageError::HashMismatch);
        assert!(Package::parse(&evil, true).is_ok());
    }

    #[test]
    fn refuses_wrong_magic_and_truncation() {
        assert_eq!(Package::parse(&[0u8; 8], false).unwrap_err(), PackageError::Truncated);
        let mut bad = FIXTURE.to_vec();
        bad[0] ^= 0xff;
        assert_eq!(Package::parse(&bad, false).unwrap_err(), PackageError::BadMagic);
    }

    #[test]
    fn admits_a_complete_guest_for_an_exact_host() {
        let guest = select_guest(FIXTURE, "psp", 1, false).unwrap();
        assert_eq!(guest.js.last(), Some(&0));
        assert!(!guest.plan.is_empty());
        assert_eq!(guest.pak[0], 10);
        assert_ne!(guest.package_hash, 0);
        assert_ne!(guest.variant_hash, 0);
    }

    #[test]
    fn rejects_target_and_host_abi_drift() {
        assert!(matches!(
            select_guest(FIXTURE, "3ds-dev", 8, false),
            Err(GuestError::MissingVariant)
        ));
        assert!(matches!(
            select_guest(FIXTURE, "psp", 8, false),
            Err(GuestError::HostAbiMismatch)
        ));
    }
}
