(function() {
  function formatCurrencyValue(raw) {
    const sanitized = raw.replace(/[^\d.]/g, '');
    if (!sanitized) {
      return '';
    }
    const parts = sanitized.split('.');
    let integerPart = parts[0] || '';
    const decimalPart = parts[1] ? parts[1].replace(/\D/g, '').slice(0, 2) : '';
    integerPart = integerPart.replace(/^0+(?=\d)/, '');
    if (integerPart.length === 0) {
      integerPart = '0';
    }
    const groups = [];
    for (let i = integerPart.length; i > 0; i -= 3) {
      const start = Math.max(i - 3, 0);
      groups.unshift(integerPart.slice(start, i));
    }
    let formatted = groups[0] || '0';
    for (let index = 1; index < groups.length; index++) {
      formatted += (index === 1 && groups.length > 2 ? "'" : ",") + groups[index];
    }
    return decimalPart ? formatted + '.' + decimalPart : formatted;
  }

  function attachFormatter(elementId) {
    const input = document.getElementById(elementId);
    if (!input) {
      return;
    }

    const formatAndRestoreCaret = () => {
      const caretPosition = input.selectionStart || 0;
      const digitsBeforeCaret = input.value.slice(0, caretPosition).replace(/\D/g, '').length;
      const formatted = formatCurrencyValue(input.value);
      input.value = formatted;
      let newCaret = 0;
      let digitsSeen = 0;
      while (newCaret < input.value.length && digitsSeen < digitsBeforeCaret) {
        if (/\d/.test(input.value.charAt(newCaret))) {
          digitsSeen++;
        }
        newCaret++;
      }
      input.setSelectionRange(newCaret, newCaret);
    };

    input.addEventListener('input', formatAndRestoreCaret);
    input.addEventListener('blur', () => {
      input.value = formatCurrencyValue(input.value);
    });
    if (input.value.trim() !== '') {
      input.value = formatCurrencyValue(input.value);
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    attachFormatter('itemCost');
    attachFormatter('editItemCost');
  });
})();
