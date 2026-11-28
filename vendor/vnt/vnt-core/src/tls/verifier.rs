use anyhow::Context;
use rustls::client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier};
use rustls::pki_types::{CertificateDer, ServerName, UnixTime};
use rustls::{CertificateError, ClientConfig, Error, SignatureScheme};
use sha2::{Digest, Sha256};
use std::fmt;
use std::str::FromStr;

#[derive(Debug)]
pub struct FingerprintVerifier {
    pub expected_fingerprint: [u8; 32],
}
impl FingerprintVerifier {
    pub fn new(expected_fingerprint: [u8; 32]) -> Self {
        Self {
            expected_fingerprint,
        }
    }
}

impl ServerCertVerifier for FingerprintVerifier {
    fn verify_server_cert(
        &self,
        end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &ServerName,
        _ocsp_response: &[u8],
        _now: UnixTime,
    ) -> Result<ServerCertVerified, Error> {
        let mut hasher = Sha256::new();
        hasher.update(end_entity.as_ref());
        let calculated_hash: [u8; 32] = hasher.finalize().into();

        if calculated_hash == self.expected_fingerprint {
            Ok(ServerCertVerified::assertion())
        } else {
            log::error!(
                "Certificate fingerprint mismatch. Expected: {:X?}, Got: {:X?}",
                self.expected_fingerprint,
                calculated_hash
            );
            Err(Error::InvalidCertificate(CertificateError::BadSignature))
        }
    }

    fn verify_tls12_signature(
        &self,
        msg: &[u8],
        cert: &CertificateDer<'_>,
        sig: &rustls::DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, Error> {
        rustls::crypto::verify_tls12_signature(
            msg,
            cert,
            sig,
            &rustls::crypto::ring::default_provider().signature_verification_algorithms,
        )
    }
    fn verify_tls13_signature(
        &self,
        msg: &[u8],
        cert: &CertificateDer<'_>,
        sig: &rustls::DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, Error> {
        rustls::crypto::verify_tls13_signature(
            msg,
            cert,
            sig,
            &rustls::crypto::ring::default_provider().signature_verification_algorithms,
        )
    }
    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        rustls::crypto::ring::default_provider()
            .signature_verification_algorithms
            .supported_schemes()
    }
}
pub fn load_root_cert() -> anyhow::Result<rustls::RootCertStore> {
    let mut root_cert_store = rustls::RootCertStore::empty();
    let certs = rustls_native_certs::load_native_certs().certs;
    for cert in certs {
        root_cert_store
            .add(cert)
            .context("Failed to add native cert to store")?;
    }
    Ok(root_cert_store)
}

#[derive(Debug, Clone, Default)]
pub enum CertValidationMode {
    InsecureSkipVerification,
    VerifyFingerprint([u8; 32]),
    #[default]
    Standard,
}
impl FromStr for CertValidationMode {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        let val = value.trim().to_lowercase();
        if val == "skip" {
            return Err("TLS certificate verification cannot be disabled".into());
        }
        if val == "standard" {
            return Ok(CertValidationMode::Standard);
        }
        if let Some(hex_str) = val.strip_prefix("finger:") {
            let decoded =
                hex::decode(hex_str).map_err(|e| format!("Invalid hex in fingerprint: {}", e))?;

            if decoded.len() != 32 {
                return Err(format!(
                    "Fingerprint must be 32 bytes (64 hex chars), got {} bytes",
                    decoded.len()
                ));
            }

            let mut arr = [0u8; 32];
            arr.copy_from_slice(&decoded);
            return Ok(CertValidationMode::VerifyFingerprint(arr));
        }
        Err(format!("Unknown certificate validation mode: {}", value))
    }
}
impl fmt::Display for CertValidationMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CertValidationMode::InsecureSkipVerification => {
                write!(f, "skip")
            }
            CertValidationMode::Standard => {
                write!(f, "standard")
            }
            CertValidationMode::VerifyFingerprint(fingerprint) => {
                let hex_str = hex::encode(fingerprint);

                write!(f, "finger:{}", hex_str)
            }
        }
    }
}

impl CertValidationMode {
    pub fn build_verifier(&self) -> anyhow::Result<std::sync::Arc<dyn ServerCertVerifier>> {
        match self {
            CertValidationMode::InsecureSkipVerification => {
                anyhow::bail!("TLS certificate verification cannot be disabled")
            }
            CertValidationMode::VerifyFingerprint(fingerprint) => {
                Ok(std::sync::Arc::new(FingerprintVerifier {
                    expected_fingerprint: *fingerprint,
                }))
            }
            CertValidationMode::Standard => {
                let root_store = load_root_cert()?;
                let verifier =
                    rustls::client::WebPkiServerVerifier::builder(std::sync::Arc::new(root_store))
                        .build()?;

                Ok(verifier)
            }
        }
    }
    pub fn create_tls_client_config(&self) -> anyhow::Result<ClientConfig> {
        let verifier = self.build_verifier()?;

        let config = ClientConfig::builder()
            .dangerous()
            .with_custom_certificate_verifier(verifier)
            .with_no_client_auth();
        Ok(config)
    }
}
