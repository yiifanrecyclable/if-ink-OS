/* 中文：手动展示切换、移动目录和滚动定位；不访问串口。 / English: Manual showcase, mobile directory and scroll position; no serial access. */
(() => {
  'use strict';
  const directory = document.getElementById('site-directory');
  const mobile = window.matchMedia('(max-width: 900px)');
  const syncDirectory = () => { directory.open = !mobile.matches; };
  syncDirectory();
  mobile.addEventListener('change', syncDirectory);
  directory.addEventListener('toggle', () => {
    if (!mobile.matches && !directory.open) directory.open = true;
  });
  const navigation = [...document.querySelectorAll('.directory a')];
  const sections = navigation.map(link => document.querySelector(link.getAttribute('href')));
  navigation.forEach(link => link.addEventListener('click', () => {
    if (mobile.matches) directory.open = false;
  }));
  let pending = false;
  const updateLocation = () => {
    const offset = mobile.matches ? 115 : 110;
    let current = 0;
    sections.forEach((section, index) => {
      if (section.getBoundingClientRect().top <= offset) current = index;
    });
    if (window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 8) current = sections.length - 1;
    navigation.forEach((link, index) => {
      if (index === current) link.setAttribute('aria-current', 'location');
      else link.removeAttribute('aria-current');
    });
    pending = false;
  };
  const scheduleLocation = () => {
    if (!pending) { pending = true; window.requestAnimationFrame(updateLocation); }
  };
  window.addEventListener('scroll', scheduleLocation, { passive: true });
  window.addEventListener('resize', scheduleLocation, { passive: true });
  window.addEventListener('load', updateLocation, { once: true });
  updateLocation();

  const tabs = [...document.querySelectorAll('.carousel-tabs [role="tab"]')];
  const slides = tabs.map(tab => document.getElementById(tab.getAttribute('aria-controls')));
  const announcement = document.getElementById('showcase-announcement');
  let active = 0;
  const selectSlide = (index, focus = false) => {
    active = (index + slides.length) % slides.length;
    tabs.forEach((tab, position) => {
      const selected = position === active;
      tab.setAttribute('aria-selected', String(selected));
      tab.tabIndex = selected ? 0 : -1;
      slides[position].hidden = !selected;
    });
    announcement.textContent = `第 ${active + 1} 张，共 ${slides.length} 张：${tabs[active].textContent}`;
    if (focus) tabs[active].focus({ preventScroll: true });
  };
  tabs.forEach((tab, index) => {
    tab.addEventListener('click', () => selectSlide(index));
    tab.addEventListener('keydown', event => {
      let next;
      if (event.key === 'ArrowRight') next = active + 1;
      if (event.key === 'ArrowLeft') next = active - 1;
      if (event.key === 'Home') next = 0;
      if (event.key === 'End') next = slides.length - 1;
      if (next !== undefined) { event.preventDefault(); selectSlide(next, true); }
    });
  });
  document.getElementById('showcase-prev').addEventListener('click', () => selectSlide(active - 1));
  document.getElementById('showcase-next').addEventListener('click', () => selectSlide(active + 1));
  const stage = document.getElementById('showcase-stage');
  let start = null;
  let suppressClick = false;
  stage.addEventListener('pointerdown', event => {
    if (event.isPrimary && event.pointerType !== 'mouse') start = { x: event.clientX, y: event.clientY, id: event.pointerId };
  }, { passive: true });
  stage.addEventListener('pointerup', event => {
    if (!start || start.id !== event.pointerId) return;
    const dx = event.clientX - start.x;
    const dy = event.clientY - start.y;
    start = null;
    if (Math.abs(dx) > 45 && Math.abs(dx) > Math.abs(dy) * 1.4) {
      suppressClick = true;
      selectSlide(active + (dx < 0 ? 1 : -1));
      window.setTimeout(() => { suppressClick = false; }, 350);
    }
  }, { passive: true });
  stage.addEventListener('pointercancel', () => { start = null; }, { passive: true });
  stage.addEventListener('click', event => {
    if (suppressClick) { event.preventDefault(); event.stopPropagation(); suppressClick = false; }
  }, true);

  const lightbox = document.getElementById('showcase-lightbox');
  const lightboxImage = document.getElementById('lightbox-image');
  const original = document.getElementById('lightbox-original');
  let priorOverflow = '';
  if (typeof lightbox.showModal === 'function') {
    document.querySelectorAll('.showcase-image').forEach(link => link.addEventListener('click', event => {
      if (event.defaultPrevented || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
      event.preventDefault();
      const source = link.querySelector('img');
      lightboxImage.src = link.href;
      lightboxImage.alt = source.alt;
      original.href = link.href;
      document.getElementById('lightbox-caption').textContent = tabs[active].textContent;
      priorOverflow = document.body.style.overflow;
      document.body.style.overflow = 'hidden';
      lightbox.showModal();
    }));
    document.getElementById('lightbox-close').addEventListener('click', () => lightbox.close());
    lightbox.addEventListener('close', () => { document.body.style.overflow = priorOverflow; });
    lightbox.addEventListener('click', event => {
      if (event.target !== lightbox) return;
      const bounds = lightbox.getBoundingClientRect();
      if (event.clientX < bounds.left || event.clientX > bounds.right || event.clientY < bounds.top || event.clientY > bounds.bottom) lightbox.close();
    });
  }
})();
