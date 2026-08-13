import { Controller } from "@hotwired/stimulus"

// Show the skin-type select only when the chosen therapy uses Table 1.
export default class extends Controller {
  static targets = ["therapy", "skinType"]

  connect() {
    this.sync()
  }

  changed() {
    this.sync()
  }

  sync() {
    const needed = this.therapyNeedsSkinType()
    if (this.hasSkinTypeTarget) {
      this.skinTypeTarget.hidden = !needed
      const select = this.skinTypeTarget.querySelector("select")
      if (select) {
        select.required = needed
        if (!needed) {
          select.value = ""
        }
      }
    }
  }

  therapyNeedsSkinType() {
    if (!this.hasTherapyTarget) {
      return false
    }
    const option = this.therapyTarget.selectedOptions[0]
    return option?.dataset?.usesSkinType === "true"
  }
}
