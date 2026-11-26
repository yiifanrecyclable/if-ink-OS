/* 中文：功能截图仅在接近视口时轮换；遵循减少动态效果设置。 / English: galleries rotate near the viewport and respect reduced motion. */
(() => {
  'use strict';
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const galleries = [...document.querySelectorAll('[data-gallery]')];
  const restartGalleries = [];

  const loadImage = image => {
    if (!image.dataset.src || image.src) return Promise.resolve();
    image.src = image.dataset.src;
    delete image.dataset.src;
    if (typeof image.decode === 'function') return image.decode().catch(() => {});
    return new Promise(resolve => {
      if (image.complete) return resolve();
      image.addEventListener('load', resolve, { once: true });
      image.addEventListener('error', resolve, { once: true });
    });
  };

  galleries.forEach(gallery => {
    const images = [...gallery.querySelectorAll('.gallery-image')];
    const dots = [...gallery.querySelectorAll('.gallery-dot')];
    if (images.length < 2 || dots.length !== images.length) return;

    let active = images.findIndex(image => image.classList.contains('is-active'));
    let timer = 0;
    let unlockTimer = 0;
    let busy = false;
    let visible = false;
    let paused = false;
    const interval = Number(gallery.dataset.interval) || 3000;

    const stop = () => {
      if (timer) window.clearInterval(timer);
      timer = 0;
    };
    const start = () => {
      stop();
      if (!visible || paused || reducedMotion.matches) return;
      timer = window.setInterval(() => select(active + 1, 1), interval);
    };
    restartGalleries.push(start);
    const select = async (requested, direction = 1) => {
      if (busy) return;
      const next = (requested + images.length) % images.length;
      if (next === active) return;
      busy = true;
      stop();
      const outgoing = images[active];
      const incoming = images[next];
      await loadImage(incoming);
      outgoing.classList.remove('exit-left', 'exit-right', 'enter-from-left', 'enter-from-right');
      incoming.classList.remove('exit-left', 'exit-right', 'enter-from-left', 'enter-from-right');
      outgoing.classList.remove('is-active');
      outgoing.classList.add(direction > 0 ? 'exit-left' : 'exit-right');
      incoming.classList.add('is-active', direction > 0 ? 'enter-from-right' : 'enter-from-left');
      incoming.setAttribute('aria-hidden', 'false');
      outgoing.setAttribute('aria-hidden', 'true');
      active = next;
      dots.forEach((dot, index) => {
        const selected = index === active;
        dot.classList.toggle('is-active', selected);
        dot.setAttribute('aria-pressed', String(selected));
      });
      window.requestAnimationFrame(() => {
        incoming.classList.remove('enter-from-left', 'enter-from-right');
      });
      if (unlockTimer) window.clearTimeout(unlockTimer);
      unlockTimer = window.setTimeout(() => {
        outgoing.classList.remove('exit-left', 'exit-right');
        busy = false;
        start();
      }, 390);
    };

    dots.forEach((dot, index) => dot.addEventListener('click', () => {
      const direction = index >= active ? 1 : -1;
      void select(index, direction);
    }));
    gallery.addEventListener('keydown', event => {
      if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
      event.preventDefault();
      void select(active + (event.key === 'ArrowRight' ? 1 : -1), event.key === 'ArrowRight' ? 1 : -1);
    });
    gallery.addEventListener('pointerenter', () => { paused = true; stop(); });
    gallery.addEventListener('pointerleave', () => { paused = false; start(); });
    gallery.addEventListener('focusin', () => { paused = true; stop(); });
    gallery.addEventListener('focusout', event => {
      if (!gallery.contains(event.relatedTarget)) { paused = false; start(); }
    });

    if ('IntersectionObserver' in window) {
      const observer = new IntersectionObserver(entries => {
        for (const entry of entries) {
          visible = entry.isIntersecting;
          if (visible) void loadImage(images[active]);
          start();
        }
      }, { rootMargin: '180px 0px' });
      observer.observe(gallery);
    } else {
      visible = true;
      start();
    }
  });

  reducedMotion.addEventListener?.('change', () => restartGalleries.forEach(restart => restart()));
})();
