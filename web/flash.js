// Shows which packaged firmware the install buttons will write.
const versionLabel = document.querySelector("#firmwareVersion");

fetch("firmware/manifest-update.json", { cache: "no-store" })
  .then((response) => {
    if (!response.ok) {
      throw new Error(response.statusText);
    }
    return response.json();
  })
  .then((manifest) => {
    versionLabel.textContent = `Firmware ${manifest.version}`;
  })
  .catch(() => {
    versionLabel.textContent = "No firmware has been packaged for this page yet.";
  });
