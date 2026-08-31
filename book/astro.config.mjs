// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import sidebar from './src/sidebar.json' with { type: 'json' };

export default defineConfig({
  site: 'https://iamjosephmj.github.io',
  base: '/ZygiskLab',
  integrations: [
    starlight({
      title: 'ZygiskLab',
      description:
        'Writing Zygisk modules: from hello-world, through injection inside a live app process, to the traces a module leaves behind.',
      customCss: ['./src/styles/custom.css'],
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/iamjosephmj/ZygiskLab' },
      ],
      head: [
        { tag: 'meta', attrs: { name: 'theme-color', content: '#161616' } },
      ],
      components: {
        // Renders the verification badge/banner from frontmatter `status`
        // instead of a hand-written span - see src/components/PageTitle.astro.
        PageTitle: './src/components/PageTitle.astro',
      },
      sidebar,
    }),
  ],
});
